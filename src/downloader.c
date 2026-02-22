/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
 * Copyright (c) 2020-2023 V10lator <v10lator@myway.de>                    *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 3 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, If not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

#include <wut-fixups.h>

#include <dirent.h>
#include <netinet/tcp.h>

#include <config.h>
#include <crypto.h>
#include <downloader.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <installer.h>
#include <ioQueue.h>
#include <localisation.h>
#include <menu/utils.h>
#include <queue.h>
#include <renderer.h>
#include <romfs.h>
#include <screen.h>
#include <state.h>
#include <staticMem.h>
#include <thread.h>
#include <ticket.h>
#include <titles.h>
#include <tmd.h>
#include <utils.h>

#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/memory.h>
#include <coreinit/time.h>
#include <curl/curl.h>
#include <nn/ac/ac_c.h>
#include <nn/result.h>
#include <nsysnet/_socket.h>
#include <nsysnet/misc.h>
#include <nsysnet/netconfig.h>
#pragma GCC diagnostic pop

#define USERAGENT        "NUSspli/" NUSSPLI_VERSION
#define SMOOTHING_FACTOR 0.2f

static bool initialised = false;
static CURL *curl;
static char curlError[CURL_ERROR_SIZE];
static bool curlReuseConnection = true;

typedef struct
{
    bool running;
    CURLcode error;
    spinlock lock;
    OSTick ts;
    curl_off_t dltotal;
    curl_off_t dlnow;
} curlProgressData;

static int progressCallback(void *rawData, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal;
    (void)ulnow;

    curlProgressData *data = (curlProgressData *)rawData;
    if(!AppRunning(false))
        data->error = CURLE_ABORTED_BY_CALLBACK;

    if(data->error != CURLE_OK)
        return 1;

    OSTick t = OSGetTick();
    if(spinTryLock(data->lock))
    {
        data->ts = t;
        data->dltotal = dltotal;
        data->dlnow = dlnow;
        spinReleaseLock(data->lock);
    }

    addEntropy(&dlnow, sizeof(curl_off_t));
    addEntropy(&t, sizeof(OSTick));
    return 0;
}

static int initSocket(void *ptr, curl_socket_t socket, curlsocktype type)
{
    (void)ptr;
    (void)type;

    int o = 1;

    // Activate WinScale
    int r = setsockopt(socket, SOL_SOCKET, SO_WINSCALE, &o, sizeof(o));
    if(r != 0)
    {
        debugPrintf("initSocket: Error settings WinScale: %d", r);
        return CURL_SOCKOPT_ERROR;
    }

    // Activate TCP SAck
    r = setsockopt(socket, SOL_SOCKET, SO_TCPSACK, &o, sizeof(o));
    if(r != 0)
    {
        debugPrintf("initSocket: Error settings TCP SAck: %d", r);
        return CURL_SOCKOPT_ERROR;
    }

    // Activate TCP nodelay - libCURL default
    r = setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &o, sizeof(o));
    if(r != 0)
    {
        debugPrintf("initSocket: Error settings TCP nodelay: %d", r);
        return CURL_SOCKOPT_ERROR;
    }

    // Disable slowstart. Should be more important fo a server but doesn't hurt a client, too
    r = setsockopt(socket, SOL_SOCKET, 0x4000, &o, sizeof(o));
    if(r != 0)
    {
        debugPrintf("initSocket: Error settings Noslowstart: %d", r);
        return CURL_SOCKOPT_ERROR;
    }

    o = 0;
    // Disable TCP keepalive - libCURL default
    r = setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &o, sizeof(o));
    if(r != 0)
    {
        debugPrintf("initSocket: Error settings TCP nodelay: %d", r);
        return CURL_SOCKOPT_ERROR;
    }

    o = IO_BUFSIZE;
    // Set send buffersize
    r = setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &o, sizeof(o));
    if(r != 0)
    {
        debugPrintf("initSocket: Error settings SBS: %d", r);
        return CURL_SOCKOPT_ERROR;
    }

    // Set receive buffersize
    r = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &o, sizeof(o));
    if(r != 0)
    {
        debugPrintf("initSocket: Error settings RBS: %d", r);
        return CURL_SOCKOPT_ERROR;
    }

    return CURL_SOCKOPT_OK;
}

static CURLcode ssl_ctx_init(CURL *cu, void *sslctx, void *parm)
{
    (void)cu;
    (void)parm;

    mbedtls_ssl_conf_rng((mbedtls_ssl_config *)sslctx, NUSrng, NULL);
    return CURLE_OK;
}

#define initNetwork() (curlReuseConnection = false)

bool initDownloader()
{
    initNetwork();

    struct curl_blob blob = { .data = NULL, .flags = CURL_BLOB_COPY };
    blob.len = readFile(ROMFS_PATH "ca-certs.pem", &blob.data);
    if(blob.data == NULL)
        return false;

    char pUrl[sizeof("http://") + 0x80 /* host */ + 0x40 /* user and pass */ + 5 /* port */ + 3 /* rest */] = "http://"; // TODO;
    char *pUrl2 = NULL;

    if(netconf_init() == 0)
    {
        NetConfProxyConfig proxy;
        if(netconf_get_proxy_config(&proxy) == 0)
        {
            if(proxy.use_proxy == NET_CONF_PROXY_ENABLED)
            {
                pUrl2 = pUrl + sizeof("http://") - 1;
                size_t ss;

                if(proxy.auth_type == NET_CONF_PROXY_AUTH_TYPE_BASIC_AUTHENTICATION)
                {
                    ss = strlen(proxy.username);
                    OSBlockMove(pUrl2, proxy.username, ss, false);
                    pUrl2 += ss;

                    *pUrl2 = ':';

                    ss = strlen(proxy.password);
                    OSBlockMove(++pUrl2, proxy.password, ss, false);
                    pUrl2 += ss;

                    *pUrl2 = '@';
                    ++pUrl2;
                }

                ss = strlen(proxy.host);
                OSBlockMove(pUrl2, proxy.host, ss, false);
                pUrl2 += ss;

                *pUrl2 = ':';
                itoa(proxy.port, ++pUrl2, 10);

                pUrl2 = pUrl;
                debugPrintf("Proxy: %s", pUrl2);
            }
        }
        else
            debugPrintf("Proxy error!");

        netconf_close();
    }
    else
        debugPrintf("Netconf error!");

    CURLcode ret = curl_global_init(CURL_GLOBAL_DEFAULT & ~(CURL_GLOBAL_SSL));
    if(ret == CURLE_OK)
    {
        curl = curl_easy_init();
        if(curl != NULL)
        {
            CURLoption opt;
#ifdef NUSSPLI_DEBUG
            curlError[0] = '\0';
            opt = CURLOPT_ERRORBUFFER;
            ret = curl_easy_setopt(curl, opt, curlError);
            if(ret == CURLE_OK)
            {
#endif
                opt = CURLOPT_SOCKOPTFUNCTION;
                ret = curl_easy_setopt(curl, opt, initSocket);
                if(ret == CURLE_OK)
                {
                    opt = CURLOPT_USERAGENT;
                    ret = curl_easy_setopt(curl, opt, USERAGENT);
                    if(ret == CURLE_OK)
                    {
                        opt = CURLOPT_XFERINFOFUNCTION;
                        ret = curl_easy_setopt(curl, opt, progressCallback);
                        if(ret == CURLE_OK)
                        {
                            opt = CURLOPT_NOPROGRESS;
                            ret = curl_easy_setopt(curl, opt, 0L);
                            if(ret == CURLE_OK)
                            {
                                opt = CURLOPT_FOLLOWLOCATION;
                                ret = curl_easy_setopt(curl, opt, 1L);
                                if(ret == CURLE_OK)
                                {
                                    opt = CURLOPT_SSL_CTX_FUNCTION;
                                    ret = curl_easy_setopt(curl, opt, ssl_ctx_init);
                                    if(ret == CURLE_OK)
                                    {
                                        opt = CURLOPT_CAINFO_BLOB;
                                        ret = curl_easy_setopt(curl, opt, blob);
                                        if(ret == CURLE_OK)
                                        {
                                            MEMFreeToDefaultHeap(blob.data);
                                            opt = CURLOPT_LOW_SPEED_LIMIT;
                                            ret = curl_easy_setopt(curl, opt, 1L);
                                            if(ret == CURLE_OK)
                                            {
                                                opt = CURLOPT_LOW_SPEED_TIME;
                                                ret = curl_easy_setopt(curl, opt, 60L);
                                                if(ret == CURLE_OK)
                                                {
                                                    opt = CURLOPT_ACCEPT_ENCODING;
                                                    ret = curl_easy_setopt(curl, opt, "");
                                                    if(ret == CURLE_OK)
                                                    {
                                                        opt = CURLOPT_PROXY;
                                                        ret = curl_easy_setopt(curl, opt, pUrl2);
                                                        if(ret == CURLE_OK)
                                                        {
                                                            initialised = true;
                                                            return true;
                                                        }
                                                    }
                                                }
                                            }

                                            blob.data = NULL;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
#ifdef NUSSPLI_DEBUG
            }
            debugPrintf("curl_easy_setopt() failed: %s (%u / %d)", curlError, opt, ret);
#endif
            curl_easy_cleanup(curl);
            curl = NULL;
        }
#ifdef NUSSPLI_DEBUG
        else
            debugPrintf("curl_easy_init() failed!");
#endif
        curl_global_cleanup();
    }

    if(blob.data != NULL)
        MEMFreeToDefaultHeap(blob.data);

    return false;
}

void deinitDownloader()
{
    if(!initialised)
        return;

    if(curl != NULL)
    {
        curl_easy_cleanup(curl);
        curl = NULL;
    }
    curl_global_cleanup();
    initialised = false;
}

static int dlThreadMain(int argc, const char **argv)
{
    debugPrintf("Download thread spawned!");
    argc = curl_easy_perform(curl);
    ((curlProgressData *)argv[0])->running = false;
    return argc;
}

static const char *translateCurlError(CURLcode err, const char *error)
{
    switch(err)
    {
        case CURLE_COULDNT_RESOLVE_HOST:
            return "Couldn't resolve hostname";
        case CURLE_COULDNT_CONNECT:
            return "Couldn't connect to server";
        case CURLE_OPERATION_TIMEDOUT:
            return "Operation timed out";
        case CURLE_GOT_NOTHING:
            return "The server didn't return any data";
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_PARTIAL_FILE:
            return "I/O error";
        case CURLE_PEER_FAILED_VERIFICATION:
            return "Verification failed";
        case CURLE_SSL_CONNECT_ERROR:
            return "Handshake failed";
        case CURLE_FAILED_INIT:
        case CURLE_READ_ERROR:
        case CURLE_OUT_OF_MEMORY:
            return "Internal error";
        case CURLE_BAD_FUNCTION_ARGUMENT: // TODO: WUT bug
            return "Internal WUT error";
        default:
            return error[0] == '\0' ? curl_easy_strerror(err) : error;
    }
}

static void drawStatLine(int line, curl_off_t totalSize, curl_off_t currentSize, float bps, uint32_t *eta)
{
    if(currentSize)
    {
        float tmp = currentSize;
        tmp /= totalSize;
        barToFrame(line, 0, 29, tmp);
        if(totalSize)
            *eta = (totalSize - currentSize) / bps;
    }
    else
        barToFrame(line, 0, 29, 0.0D);

    char *toScreen = getToFrameBuffer();
    humanize(currentSize, toScreen);
    char *ptr = toScreen + strlen(toScreen);
    strcpy(ptr, " / ");
    ptr += 3;
    humanize(totalSize, ptr);
    textToFrame(line, 30, toScreen);

    secsToTime(*eta, toScreen);
    textToFrame(line, ALIGNED_RIGHT, toScreen);
}

typedef struct
{
    char url[256];
    char file[FS_MAX_PATH];
    downloadData *data;
    FileType type;
    bool resume;
    QUEUE_DATA *queueData;
    RAMBUF *rambuf;
    ResultCallback callback;
    void *userdata;

    // State
    int state;
    FILE *fp;
    size_t fileSize;
    volatile curlProgressData cdata;
    OSThread *dlThread;
    OSTime t;
    OSTick lastTransfair;
    size_t downloaded;
    float oldBps;
    int frames;
    int result;
    int networkErrorFrames;
    char networkErrorMsg[1024];
} DownloadFileData;

static void downloadFileDraw(Screen *self)
{
    DownloadFileData *data = (DownloadFileData *)self->data;
    char *toScreen = getToFrameBuffer();
    int line;

    startNewFrame();

    if(data->state == 3) // Network error display
    {
        drawErrorFrame(data->networkErrorMsg, B_RETURN | Y_RETRY);
        if(autoResumeEnabled())
        {
            int s = data->networkErrorFrames / 60;
            char *p = strchr(data->networkErrorMsg, '_'); // Hacky
            if(p) *p = '0' + s;
        }
    }
    else
    {
        OSTick ts = data->cdata.ts;
        curl_off_t dltotal = data->cdata.dltotal;
        curl_off_t dlnow = data->cdata.dlnow;

        float bps = (float)(dlnow - data->downloaded);
        data->downloaded = dlnow;
        dlnow += data->fileSize;

        if(bps != 0.0f)
        {
            if(dltotal)
            {
                uint32_t tmp = OSTicksToMilliseconds(ts - data->lastTransfair);
                if(tmp)
                {
                    bps *= 1000.0f;
                    bps /= tmp;
                    bps *= 1.0f - SMOOTHING_FACTOR;
                    data->oldBps *= SMOOTHING_FACTOR;
                    bps += data->oldBps;
                    data->oldBps = bps;
                }
                else bps = 0.0f;
            }
            else bps = 0.0f;
        }
        data->lastTransfair = ts;

        if(data->data != NULL)
        {
            if(data->queueData != NULL)
            {
                sprintf(toScreen, "%s (%d/%d)", data->data->name, data->queueData->current, data->queueData->packages);
                line = textToFrameMultiline(0, ALIGNED_CENTER, toScreen, MAX_CHARS);
            }
            else
                line = textToFrameMultiline(0, ALIGNED_CENTER, data->data->name, MAX_CHARS);

            drawStatLine(line++, data->data->dltotal, data->data->dlnow + dlnow, bps, &data->data->eta);
            if(data->queueData != NULL)
                drawStatLine(line++, data->queueData->dlSize, data->queueData->downloaded + dlnow, bps, &data->queueData->eta);

            lineToFrame(line++, SCREEN_COLOR_WHITE);
            sprintf(toScreen, "(%d/%d)", data->data->dcontent + 1, data->data->contents);
            textToFrame(line, ALIGNED_CENTER, toScreen);
        }
        else line = 0;

        if(dltotal)
        {
            if(!data->rambuf) checkForQueueErrors();
            dltotal += data->fileSize;
            const char *name = data->rambuf ? data->file : strrchr(data->file, '/') + 1;
            sprintf(toScreen, "%s %s", localise("Downloading"), name);
            textToFrame(line, 0, toScreen);
            getSpeedString(bps, toScreen);
            textToFrame(line, ALIGNED_RIGHT, toScreen);
            uint32_t tmp;
            drawStatLine(++line, dltotal, dlnow, bps, &tmp);
        }
        else
        {
            const char *name = data->rambuf ? data->file : strrchr(data->file, '/') + 1;
            sprintf(toScreen, "%s %s", localise("Preparing"), name);
            textToFrame(line++, 0, toScreen);
        }
        writeScreenLog(++line);
    }
    drawFrame();
}

static void downloadFileUpdate(Screen *self)
{
    DownloadFileData *data = (DownloadFileData *)self->data;

    if(data->state == 0) // Initialize
    {
        if(data->rambuf)
        {
            data->fp = (void *)open_memstream(&data->rambuf->buf, &data->rambuf->size);
            data->fileSize = 0;
        }
        else
        {
            if(data->resume && fileExists(data->file))
            {
                data->fileSize = getFilesize(data->file);
                if(data->fileSize != 0)
                {
                    if(data->data != NULL && data->data->cs)
                    {
                        if(data->fileSize == data->data->cs)
                        {
                            data->data->dlnow += data->fileSize;
                            if(data->queueData) data->queueData->downloaded += data->fileSize;
                            ResultCallback cb = data->callback;
                            void *ud = data->userdata;
                            screenPop();
                            if(cb) cb(true, ud);
                            return;
                        }
                        if(data->fileSize > data->data->cs) data->fileSize = 0; // Restart
                    }
                }
            }
            else data->fileSize = 0;
            data->fp = (void *)openFile(data->file, data->fileSize ? "a" : "w", data->data ? data->data->cs : 0);
        }

        if(data->fp == NULL)
        {
            ResultCallback cb = data->callback;
            void *ud = data->userdata;
            screenPop();
            if(cb) cb(false, ud);
            return;
        }

        curlError[0] = '\0';
        data->cdata.running = true;
        data->cdata.error = CURLE_OK;
        data->cdata.dlnow = 0;
        data->cdata.dltotal = 0;
        spinCreateLock(data->cdata.lock, SPINLOCK_FREE);

        curl_easy_setopt(curl, CURLOPT_URL, data->url);
        curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, curlReuseConnection ? 0L : 1L);
        curlReuseConnection = true;
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)data->fileSize);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, data->rambuf ? fwrite : (size_t(*)(const void *, size_t, size_t, FILE *))addToIOQueue);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, data->fp);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)&data->cdata);

        data->t = OSGetSystemTime();
        char *argv[1] = { (char *)&data->cdata };
        data->dlThread = startThread("NUSspli downloader", THREAD_PRIORITY_HIGH, STACKSIZE_BIG, dlThreadMain, 1, (char *)argv, OS_THREAD_ATTRIB_AFFINITY_CPU0);
        if(data->dlThread == NULL)
        {
            if(data->rambuf) fclose(data->fp); else addToIOQueue(NULL, 0, 0, (FSAFileHandle)data->fp);
            ResultCallback cb = data->callback;
            void *ud = data->userdata;
            screenPop();
            if(cb) cb(false, ud);
            return;
        }
        data->lastTransfair = OSGetTick();
        data->downloaded = 0;
        data->oldBps = 0;
        data->frames = 1;
        data->state = 1; // Downloading
    }
    else if(data->state == 1) // Downloading
    {
        if(!data->cdata.running)
        {
            CURLcode ret;
            stopThread(data->dlThread, (int *)&ret);
            data->dlThread = NULL;
            if(data->rambuf) fclose(data->fp); else addToIOQueue(NULL, 0, 0, (FSAFileHandle)data->fp);
            data->fp = NULL;

            if(ret != CURLE_OK)
            {
                // Handle error
                const char *te = translateCurlError(ret, curlError);
                char *toScreen = data->networkErrorMsg;
                switch(ret)
                {
                    case CURLE_RANGE_ERROR:
                        if(data->rambuf) { MEMFreeToDefaultHeap(data->rambuf->buf); data->rambuf->buf = NULL; data->rambuf->size = 0; }
                        data->state = 0; // Retry from start
                        return;
                    case CURLE_COULDNT_RESOLVE_HOST:
                    case CURLE_COULDNT_CONNECT:
                    case CURLE_OPERATION_TIMEDOUT:
                    case CURLE_GOT_NOTHING:
                    case CURLE_SEND_ERROR:
                    case CURLE_RECV_ERROR:
                    case CURLE_PARTIAL_FILE:
                    case CURLE_BAD_FUNCTION_ARGUMENT:
                        sprintf(toScreen, "%s:\n\t%s\n\n%s", "Network error", te, "check the network settings and try again");
                        break;
                    case CURLE_PEER_FAILED_VERIFICATION:
                    case CURLE_SSL_CONNECT_ERROR:
                        sprintf(toScreen, "%s:\n\t%s!\n\n%s", "SSL error", te, "check your Wii Us date and time settings");
                        break;
                    default:
                        sprintf(toScreen, "%s:\n\t%d %s", te, ret, curlError);
                        break;
                }

                if(autoResumeEnabled())
                {
                    data->networkErrorFrames = 9 * 60;
                    strcat(toScreen, "\n\n");
                    strcat(toScreen, localise("Next try in _ seconds."));
                }
                data->state = 3; // Error display
                return;
            }

            long resp;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp);
            if(resp == 206) resp = 200;

            if(resp != 200)
            {
                if(!data->rambuf) { flushIOQueue(); FSARemove(getFSAClient(), data->file); }
                if(resp == 404 && (data->type & FILE_TYPE_TIK) == FILE_TYPE_TIK)
                {
                    ResultCallback cb = data->callback;
                    void *ud = data->userdata;
                    screenPop();
                    if(cb) cb(2, ud); // Need fake ticket
                    return;
                }
                sprintf(data->networkErrorMsg, "%s: %ld\n%s: %s", localise("The download returned a result different to 200 (OK)"), resp, localise("File"), data->rambuf ? data->file : prettyDir(data->file));
                data->state = 3;
                return;
            }

            if(data->data)
            {
                curl_off_t dld;
                curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &dld);
                data->data->dlnow += (dld + data->fileSize);
                if(data->queueData) data->queueData->downloaded += (dld + data->fileSize);
            }
            ResultCallback cb = data->callback;
            void *ud = data->userdata;
            screenPop();
            if(cb) cb(true, ud);
        }
        else if(vpad.trigger & VPAD_BUTTON_B)
        {
            // Cancel?
            data->cdata.error = CURLE_ABORTED_BY_CALLBACK;
        }
    }
    else if(data->state == 3) // Error display
    {
        bool retry = false;
        if(vpad.trigger & VPAD_BUTTON_B)
        {
            ResultCallback cb = data->callback;
            void *ud = data->userdata;
            screenPop();
            if(cb) cb(false, ud);
            return;
        }
        if(vpad.trigger & VPAD_BUTTON_Y) retry = true;
        if(autoResumeEnabled() && --data->networkErrorFrames == 0) retry = true;

        if(retry)
        {
            // Reset network logic
            BOOL con;
            if(ACIsApplicationConnected(&con).value == 0 && !con)
            {
                deinitDownloader();
                socket_lib_finish();
                NNResult nnres = ACClose();
                while(ACGetCloseStatus(nnres).value != 0);
                nnres = ACConnect();
                if(nnres.value == 0)
                {
                    socket_lib_init();
                    initDownloader();
                }
            }
            data->state = 0;
            self->dirty = true;
        }
    }
}

static void downloadFileExit(Screen *self)
{
    DownloadFileData *data = (DownloadFileData *)self->data;
    if(data->dlThread) stopThread(data->dlThread, NULL);
    if(data->fp) { if(data->rambuf) fclose(data->fp); else addToIOQueue(NULL, 0, 0, (FSAFileHandle)data->fp); }
    MEMFreeToDefaultHeap(data);
    MEMFreeToDefaultHeap(self);
}

void downloadFile(const char *url, char *file, downloadData *data, FileType type, bool resume, QUEUE_DATA *queueData, RAMBUF *rambuf, ResultCallback callback, void *userdata)
{
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    DownloadFileData *d = MEMAllocFromDefaultHeap(sizeof(DownloadFileData));
    if(!self || !d) { if(self) MEMFreeToDefaultHeap(self); if(d) MEMFreeToDefaultHeap(d); if(callback) callback(false, userdata); return; }

    OSBlockSet(d, 0, sizeof(DownloadFileData));
    strncpy(d->url, url, 255);
    strncpy(d->file, file, FS_MAX_PATH - 1);
    d->data = data;
    d->type = type;
    d->resume = resume;
    d->queueData = queueData;
    d->rambuf = rambuf;
    d->callback = callback;
    d->userdata = userdata;
    d->state = 0;

    self->onUpdate = downloadFileUpdate;
    self->onDraw = downloadFileDraw;
    self->onExit = downloadFileExit;
    self->data = d;
    self->dirty = true;

    screenPush(self);
}

typedef struct
{
    TMD *tmd;
    size_t tmdSize;
    const TitleEntry *titleEntry;
    char titleVer[33];
    char folderName[FS_MAX_PATH];
    bool inst;
    NUSDEV dlDev;
    bool toUSB;
    bool keepFiles;
    QUEUE_DATA *queueData;
    ResultCallback callback;
    void *userdata;

    // State
    int state;
    int contentIdx;
    char installDir[FS_MAX_PATH];
    char downloadUrl[256];
    downloadData data;
    RAMBUF *tikBuf;
} DownloadTitleData;

static void downloadTitleTaskDone(bool result, void *userdata);

static void certDone(bool result, void *userdata)
{
    (void)result;
    DownloadTitleData *data = (DownloadTitleData *)userdata;
    data->state = 3; // Proceed to download contents
}

static void downloadTitleUpdate(Screen *self)
{
    DownloadTitleData *data = (DownloadTitleData *)self->data;
    char tid[17];
    hex(data->tmd->tid, 16, tid);

    switch(data->state)
    {
        case 0: // Init dirs
        {
            strcpy(data->downloadUrl, DOWNLOAD_URL);
            strcat(data->downloadUrl, tid);
            strcat(data->downloadUrl, "/");

            if(data->folderName[0] == '\0')
            {
                size_t i;
                for(i = 0; i < strlen(data->titleEntry->name); ++i)
                    data->folderName[i] = isAllowedInFilename(data->titleEntry->name[i]) ? data->titleEntry->name[i] : '_';
                data->folderName[i] = '\0';
            }
            strcat(data->folderName, " [");
            strcat(data->folderName, tid);
            strcat(data->folderName, "]");
            if(data->titleVer[0]) { strcat(data->folderName, " v"); strcat(data->folderName, data->titleVer); }

            strcpy(data->installDir, data->dlDev == NUSDEV_USB01 ? INSTALL_DIR_USB1 : (data->dlDev == NUSDEV_USB02 ? INSTALL_DIR_USB2 : (data->dlDev == NUSDEV_SD ? INSTALL_DIR_SD : INSTALL_DIR_MLC)));
            if(!dirExists(data->installDir)) createDirectory(data->installDir);
            strcat(data->installDir, data->folderName);
            strcat(data->installDir, "/");
            if(!dirExists(data->installDir)) createDirectory(data->installDir);

            char tmdPath[FS_MAX_PATH];
            strcpy(tmdPath, data->installDir);
            strcat(tmdPath, "title.tmd");
            FSAFileHandle fp = openFile(tmdPath, "w", data->tmdSize);
            if(fp) { addToIOQueue(data->tmd, 1, data->tmdSize, fp); addToIOQueue(NULL, 0, 0, fp); }

            data->data.name = data->titleEntry->name;
            data->data.contents = data->tmd->num_contents + 1;
            data->data.dcontent = 0;
            data->data.dlnow = 0;
            data->data.dltotal = 0;
            data->data.eta = -1;

            for(int i = 0; i < data->tmd->num_contents; ++i)
            {
                data->data.dltotal += data->tmd->contents[i].size;
                if(data->tmd->contents[i].type & TMD_CONTENT_TYPE_HASHED)
                {
                    data->data.contents++;
                    data->data.dltotal += getH3size(data->tmd->contents[i].size);
                }
            }

            data->state = 1; // Download TIK
            char tikUrl[256], tikPath[FS_MAX_PATH];
            strcpy(tikUrl, data->downloadUrl); strcat(tikUrl, "cetk");
            strcpy(tikPath, data->installDir); strcat(tikPath, "title.tik");
            if(!fileExists(tikPath))
            {
                data->tikBuf = allocRamBuf();
                downloadFile(tikUrl, tikPath, &data->data, FILE_TYPE_TIK | FILE_TYPE_TORAM, false, data->queueData, data->tikBuf, downloadTitleTaskDone, self);
            }
            else data->state = 2;
            break;
        }
        case 2: // Cert
        {
            char certPath[FS_MAX_PATH];
            strcpy(certPath, data->installDir); strcat(certPath, "title.cert");
            if(!fileExists(certPath))
            {
                data->state = 20; // Waiting for cert
                generateCert(data->tmd, (TICKET *)data->tikBuf->buf, data->tikBuf->size, certPath, certDone, data);
                return;
            }
            data->state = 3;
            data->contentIdx = 0;
            break;
        }
        case 3: // Download contents
        {
            if(data->contentIdx >= data->tmd->num_contents)
            {
                data->state = 4; // Install
                return;
            }
            char cid[9], appUrl[256], appPath[FS_MAX_PATH];
            hex(data->tmd->contents[data->contentIdx].cid, 8, cid);
            strcpy(appUrl, data->downloadUrl); strcat(appUrl, cid);
            strcpy(appPath, data->installDir); strcat(appPath, cid); strcat(appPath, ".app");
            data->data.dcontent++;
            data->data.cs = data->tmd->contents[data->contentIdx].size;
            data->state = 31; // Downloading .app
            downloadFile(appUrl, appPath, &data->data, FILE_TYPE_APP, true, data->queueData, NULL, downloadTitleTaskDone, self);
            break;
        }
        case 32: // Download .h3
        {
            if(data->tmd->contents[data->contentIdx].type & TMD_CONTENT_TYPE_HASHED)
            {
                char cid[9], h3Url[256], h3Path[FS_MAX_PATH];
                hex(data->tmd->contents[data->contentIdx].cid, 8, cid);
                strcpy(h3Url, data->downloadUrl); strcat(h3Url, cid); strcat(h3Url, ".h3");
                strcpy(h3Path, data->installDir); strcat(h3Path, cid); strcat(h3Path, ".h3");
                data->data.dcontent++;
                data->data.cs = getH3size(data->tmd->contents[data->contentIdx].size);
                data->state = 33; // Downloading .h3
                downloadFile(h3Url, h3Path, &data->data, FILE_TYPE_H3, true, data->queueData, NULL, downloadTitleTaskDone, self);
            }
            else
            {
                data->contentIdx++;
                data->state = 3;
            }
            break;
        }
        case 4: // Install
        {
            ResultCallback cb = data->callback;
            void *ud = data->userdata;
            if(data->inst)
            {
                bool hasDependencies = false;
                switch(getTidHighFromTid(data->tmd->tid))
                {
                    case TID_HIGH_DLC: case TID_HIGH_UPDATE: hasDependencies = true; break;
                }
                const char *titleName = data->titleEntry->name;
                NUSDEV dlDev = data->dlDev;
                char *installDir = MEMAllocFromDefaultHeap(strlen(data->installDir) + 1);
                strcpy(installDir, data->installDir);
                bool toUSB = data->toUSB;
                bool keepFiles = data->keepFiles;
                TMD *tmd = data->tmd;
                data->tmd = NULL; // Hand over ownership
                screenPop();
                install(titleName, hasDependencies, dlDev, installDir, toUSB, keepFiles, tmd, cb, ud);
                MEMFreeToDefaultHeap(installDir);
            }
            else
            {
               screenPop();
               if(cb) cb(true, ud);
            }
            break;
        }
        default: break;
    }
}

static void downloadTitleTaskDone(bool result, void *userdata)
{
    Screen *self = (Screen *)userdata;
    DownloadTitleData *data = (DownloadTitleData *)self->data;
    if(result == 2 && data->state == 1) // Need fake ticket
    {
        char tikPath[FS_MAX_PATH];
        strcpy(tikPath, data->installDir); strcat(tikPath, "title.tik");
        generateTik(tikPath, data->tmd);
        data->state = 2;
        return;
    }
    if(!result)
    {
        ResultCallback cb = data->callback;
        void *ud = data->userdata;
        screenPop();
        if(cb) cb(false, ud);
        return;
    }

    if(data->state == 1) {
        char tikPath[FS_MAX_PATH];
        strcpy(tikPath, data->installDir); strcat(tikPath, "title.tik");
        FSAFileHandle fp = openFile(tikPath, "w", data->tikBuf->size);
        if(fp) { addToIOQueue(data->tikBuf->buf, 1, data->tikBuf->size, fp); addToIOQueue(NULL, 0, 0, fp); }
        data->state = 2;
    }
    else if(data->state == 31) data->state = 32;
    else if(data->state == 33) { data->contentIdx++; data->state = 3; }
}

static void downloadTitleExit(Screen *self)
{
    DownloadTitleData *data = (DownloadTitleData *)self->data;
    if(data->tikBuf) freeRamBuf(data->tikBuf);
    if(data->tmd) MEMFreeToDefaultHeap(data->tmd);
    MEMFreeToDefaultHeap(data);
    MEMFreeToDefaultHeap(self);
}

void downloadTitle(const TMD *tmd, size_t tmdSize, const TitleEntry *titleEntry, const char *titleVer, char *folderName, bool inst, NUSDEV dlDev, bool toUSB, bool keepFiles, QUEUE_DATA *queueData, ResultCallback callback, void *userdata)
{
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    DownloadTitleData *data = MEMAllocFromDefaultHeap(sizeof(DownloadTitleData));
    if(!self || !data) { if(self) MEMFreeToDefaultHeap(self); if(data) MEMFreeToDefaultHeap(data); if(callback) callback(false, userdata); return; }

    OSBlockSet(data, 0, sizeof(DownloadTitleData));
    data->tmd = (TMD *)MEMAllocFromDefaultHeap(tmdSize);
    memcpy(data->tmd, tmd, tmdSize);
    data->tmdSize = tmdSize;
    data->titleEntry = titleEntry;
    strncpy(data->titleVer, titleVer, 32);
    if(folderName) strncpy(data->folderName, folderName, FS_MAX_PATH - 1);
    data->inst = inst;
    data->dlDev = dlDev;
    data->toUSB = toUSB;
    data->keepFiles = keepFiles;
    data->queueData = queueData;
    data->callback = callback;
    data->userdata = userdata;
    data->state = 0;

    self->onUpdate = downloadTitleUpdate;
    self->onDraw = NULL;
    self->onExit = downloadTitleExit;
    self->data = data;
    self->dirty = false;

    screenPush(self);
}

RAMBUF *allocRamBuf()
{
    RAMBUF *ret = MEMAllocFromDefaultHeap(sizeof(RAMBUF));
    if(ret == NULL) return NULL;
    ret->buf = NULL;
    ret->size = 0;
    return ret;
}

void freeRamBuf(RAMBUF *rambuf)
{
    if(rambuf->buf != NULL) MEMFreeToDefaultHeap(rambuf->buf);
    MEMFreeToDefaultHeap(rambuf);
}
