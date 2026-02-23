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

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <config.h>
#include <deinstaller.h>
#include <downloader.h>
#include <filesystem.h>
#include <input.h>
#include <installer.h>
#include <menu/predownload.h>
#include <menu/utils.h>
#include <queue.h>
#include <renderer.h>
#include <screen.h>
#include <state.h>
#include <titles.h>
#include <tmd.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

#define PD_MENU_ENTRIES 5

typedef struct
{
    const TitleEntry *entry;
    RAMBUF *rambuf;
    MCPTitleListType titleList;
    bool installed;
    char folderName[FS_MAX_PATH - 11];
    char titleVer[33];
    uint64_t dls;
    int cursorPos;
    OPERATION operation;
    bool keepFiles;
    NUSDEV dlDev;
    NUSDEV instDev;
    bool autoAddToQueue;
    bool autoStartQueue;
    TMD *tmd;
    int state;
} PDData;

static inline bool isInstalled(const TitleEntry *entry, MCPTitleListType *out)
{
    if(out == NULL)
    {
        MCPTitleListType titleList __attribute__((__aligned__(0x40)));
        return MCP_GetTitleInfo(mcpHandle, entry->tid, &titleList) == 0;
    }
    return MCP_GetTitleInfo(mcpHandle, entry->tid, out) == 0;
}

static void drawPDMenuFrame(PDData *data)
{
    startNewFrame();

    textToFrame(0, 0, localise("Name:"));

    char *toFrame = getToFrameBuffer();
    strcpy(toFrame, data->entry->name);
    char tid[17];
    hex(data->entry->tid, 16, tid);
    strcat(toFrame, " [");
    strcat(toFrame, tid);
    strcat(toFrame, "]");
    int line = textToFrameMultiline(0, ALIGNED_CENTER, toFrame, MAX_CHARS - 33);

    humanize(data->dls, toFrame);

    textToFrame(++line, 0, localise("Region:"));
    flagToFrame(++line, 3, data->entry->region);
    textToFrame(line, 7, localise(getFormattedRegion(data->entry->region)));

    textToFrame(++line, 0, localise("Size:"));
    textToFrame(++line, 3, toFrame);

    strcpy(toFrame, localise("Provided title version"));
    strcat(toFrame, " [");
    strcat(toFrame, localise("Only numbers"));
    strcat(toFrame, "]:");
    textToFrame(++line, 0, toFrame);

    if(data->titleVer[0] == u'\0')
    {
        toFrame[0] = '<';
        strcpy(toFrame + 1, localise("LATEST"));
        strcat(toFrame, ">");
        textToFrame(++line, 3, toFrame);
    }
    else
        textToFrame(++line, 3, data->titleVer);

    strcpy(toFrame, localise("Custom folder name"));
    strcat(toFrame, " [");
    strcat(toFrame, localise("ASCII only"));
    strcat(toFrame, "]:");
    textToFrame(++line, 0, toFrame);
    textToFrame(++line, 3, data->folderName);

    line = MAX_LINES;

    arrowToFrame(data->cursorPos, 0);

    strcpy(toFrame, localise(BUTTON_MINUS " to add to the queue"));
    if(data->installed)
    {
        strcat(toFrame, " || ");
        strcat(toFrame, localise(BUTTON_Y " to uninstall"));
    }
    textToFrame(--line, ALIGNED_CENTER, toFrame);

    strcpy(toFrame, localise("Press " BUTTON_B " to return"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_PLUS " to start"));
    textToFrame(--line, ALIGNED_CENTER, toFrame);

    lineToFrame(--line, SCREEN_COLOR_WHITE);

    textToFrame(--line, 4, localise("Set custom name to the download folder"));
    textToFrame(--line, 4, localise("Set title version"));

    if(data->operation == OPERATION_DOWNLOAD)
        data->keepFiles = true;
    else
    {
        switch((int)data->dlDev)
        {
            case NUSDEV_USB01:
            case NUSDEV_USB02:
            case NUSDEV_MLC:
                data->keepFiles = false;
        }
    }

    strcpy(toFrame, localise("Keep downloaded files:"));
    strcat(toFrame, " ");
    strcat(toFrame, localise(data->keepFiles ? "Yes" : "No"));
    if(data->dlDev == NUSDEV_SD && data->operation == OPERATION_DOWNLOAD_INSTALL)
        textToFrame(--line, 4, localise(toFrame));
    else
        textToFrameColored(--line, 4, localise(toFrame), SCREEN_COLOR_WHITE_TRANSP);

    strcpy(toFrame, localise("Download to:"));
    strcat(toFrame, " ");
    switch((int)data->dlDev)
    {
        case NUSDEV_USB01:
        case NUSDEV_USB02:
            strcat(toFrame, "USB");
            break;
        case NUSDEV_SD:
            strcat(toFrame, "SD");
            break;
        case NUSDEV_MLC:
            strcat(toFrame, "NAND");
    }

    getFreeSpaceString(data->dlDev, toFrame + strlen(toFrame));
    textToFrame(--line, 4, localise(toFrame));

    strcpy(toFrame, localise("Operation:"));
    strcat(toFrame, " ");
    switch((int)data->operation)
    {
        case OPERATION_DOWNLOAD:
            strcat(toFrame, localise("Download only"));
            break;
        case OPERATION_DOWNLOAD_INSTALL:
            strcat(toFrame, localise("Install"));
            break;
    }
    textToFrame(--line, 4, toFrame);

    strcpy(toFrame, localise("Install to:"));
    strcat(toFrame, " ");
    switch((int)data->instDev)
    {
        case NUSDEV_USB01:
        case NUSDEV_USB02:
            strcat(toFrame, "USB");
            break;
        case NUSDEV_MLC:
            strcat(toFrame, "NAND");
            break;
    }

    getFreeSpaceString(data->instDev, toFrame + strlen(toFrame));

    if(data->operation == OPERATION_DOWNLOAD_INSTALL)
        textToFrame(--line, 4, toFrame);
    else
        textToFrameColored(--line, 4, toFrame, SCREEN_COLOR_WHITE_TRANSP);

    lineToFrame(--line, SCREEN_COLOR_WHITE);

    drawFrame();
}

static void downloadTMDDone(bool result, void *userdata)
{
    Screen *self = (Screen *)userdata;
    PDData *data = (PDData *)self->data;
    if(!result)
    {
        saveConfig(false);
        screenPop();
        return;
    }

    data->tmd = (TMD *)data->rambuf->buf;
    if(verifyTmd(data->tmd, data->rambuf->size) != TMD_STATE_GOOD)
    {
        saveConfig(false);
        showErrorFrame(localise("Invalid title.tmd file!"));
        screenPop();
        return;
    }

    data->dls = 0;
    for(uint16_t i = 0; i < data->tmd->num_contents; ++i)
    {
        if(data->tmd->contents[i].type & TMD_CONTENT_TYPE_HASHED)
            data->dls += getH3size(data->tmd->contents[i].size);
        data->dls += data->tmd->contents[i].size;
    }
    data->state = 2; // Menu ready
    self->dirty = true;
}

static void downloadTMD(Screen *self)
{
    PDData *data = (PDData *)self->data;
    if(data->rambuf != NULL)
        freeRamBuf(data->rambuf);

    data->rambuf = allocRamBuf();
    if(data->rambuf == NULL)
        return;

    char tid[17];
    char downloadUrl[256];
    hex(data->entry->tid, 16, tid);

    debugPrintf("Downloading TMD...");
    strcpy(downloadUrl, DOWNLOAD_URL);
    strcat(downloadUrl, tid);
    strcat(downloadUrl, "/tmd");

    if(strlen(data->titleVer) > 0)
    {
        strcat(downloadUrl, ".");
        strcat(downloadUrl, data->titleVer);
    }

    data->state = 1; // Downloading TMD
    downloadFile(downloadUrl, "title.tmd", NULL, (FileType)(FILE_TYPE_TMD | FILE_TYPE_TORAM), false, NULL, data->rambuf, downloadTMDDone, self);
}

static bool addToOpQueue(PDData *data)
{
    TitleData *titleInfo = MEMAllocFromDefaultHeap(sizeof(TitleData));
    int ret = false;
    if(titleInfo != NULL)
    {
        titleInfo->tmd = (TMD *)data->rambuf->buf;
        titleInfo->tmdSize = data->rambuf->size;
        titleInfo->rambuf = data->rambuf;
        titleInfo->entry = data->entry;
        strcpy(titleInfo->titleVer, data->titleVer);
        strcpy(titleInfo->folderName, data->folderName);
        titleInfo->operation = data->operation;
        titleInfo->dlDev = data->dlDev;
        titleInfo->toUSB = data->instDev & NUSDEV_USB;
        titleInfo->keepFiles = data->keepFiles;

        ret = addToQueue(titleInfo);
        if(ret == 1)
        {
            data->rambuf = NULL;
            return true;
        }

        MEMFreeToDefaultHeap(titleInfo);
    }

    return ret;
}

static void titleVerCallback(bool ok, const char *text, void *userdata)
{
    Screen *self = (Screen *)userdata;
    PDData *d = (PDData *)self->data;
    if(ok && text)
        strcpy(d->titleVer, text);
    else
        d->titleVer[0] = '\0';
    downloadTMD(self);
}

static void folderNameCallback(bool ok, const char *text, void *userdata)
{
    Screen *self = (Screen *)userdata;
    PDData *d = (PDData *)self->data;
    if(ok && text)
        strcpy(d->folderName, text);
}

static void downloadTitleDone(bool result, void *userdata)
{
    char *name = (char *)userdata;
    enableApd();
    if(result)
        showFinishedScreen(name, FINISHING_OPERATION_INSTALL); // TODO: check op
    if(name)
        MEMFreeToDefaultHeap(name);
}

static void checkSystemCallback(bool result, void *userdata)
{
    PDData *data = (PDData *)userdata;
    if(result)
    {
        char *name = MEMAllocFromDefaultHeap(strlen(data->entry->name) + 1);
        if(name)
            strcpy(name, data->entry->name);

        TMD *tmd = data->tmd;
        size_t tmdSize = data->rambuf->size;
        const TitleEntry *entry = data->entry;
        char titleVer[33];
        strcpy(titleVer, data->titleVer);
        char folderName[FS_MAX_PATH];
        strcpy(folderName, data->folderName);
        bool inst = data->operation == OPERATION_DOWNLOAD_INSTALL;
        NUSDEV dlDev = data->dlDev;
        bool toUSB = (data->instDev & NUSDEV_USB) != 0;
        bool keepFiles = data->keepFiles;

        RAMBUF *rb = data->rambuf;
        data->tmd = NULL;
        data->rambuf = NULL; // Hand over ownership

        disableApd();
        screenPop(); // Pop predownload screen
        downloadTitle(tmd, tmdSize, entry, titleVer, folderName, inst, dlDev, toUSB, keepFiles, NULL, downloadTitleDone, name);
        MEMFreeToDefaultHeap(tmd);
        MEMFreeToDefaultHeap(rb);
    }
}

static void startOpCallback(bool result, void *userdata)
{
    PDData *data = (PDData *)userdata;
    if(!result)
        return;

    checkSystemTitleFromEntry(data->entry, false, checkSystemCallback, data);
}

static void deinstallCallback(bool result, void *userdata)
{
    PDData *data = (PDData *)userdata;
    if(result)
    {
        char *name = MEMAllocFromDefaultHeap(strlen(data->entry->name) + 1);
        if(name)
            strcpy(name, data->entry->name);
        MCPTitleListType titleList = data->titleList;

        screenPop(); // Pop predownload screen
        deinstall(&titleList, name, false, false, NULL, NULL);
        if(name)
            MEMFreeToDefaultHeap(name);
    }
}

static void predownloadUpdate(Screen *self)
{
    PDData *data = (PDData *)self->data;

    if(data->state == 1)
        return; // Downloading TMD

    if(vpad.trigger & VPAD_BUTTON_B)
    {
        saveConfig(false);
        screenPop();
        return;
    }

    if(vpad.trigger & (VPAD_BUTTON_RIGHT | VPAD_BUTTON_LEFT | VPAD_BUTTON_A))
    {
        switch(data->cursorPos)
        {
            case 15:
                if(data->operation == OPERATION_DOWNLOAD_INSTALL)
                {
                    switch((int)data->instDev)
                    {
                        case NUSDEV_USB01:
                        case NUSDEV_USB02:
                            data->instDev = NUSDEV_MLC;
                            break;
                        case NUSDEV_MLC:
                            data->instDev = getUSB();
                            if(!data->instDev)
                                data->instDev = NUSDEV_MLC;
                            break;
                    }
                }
                break;
            case 16:
                data->operation = (data->operation == OPERATION_DOWNLOAD_INSTALL) ? OPERATION_DOWNLOAD : OPERATION_DOWNLOAD_INSTALL;
                break;
            case 17:
            {
                bool toUSB = false;
                if(vpad.trigger & VPAD_BUTTON_LEFT)
                {
                    switch((int)data->dlDev)
                    {
                        case NUSDEV_USB01:
                        case NUSDEV_USB02:
                            data->dlDev = NUSDEV_MLC;
                            break;
                        case NUSDEV_MLC:
                            data->dlDev = NUSDEV_SD;
                            break;
                        case NUSDEV_SD:
                            data->dlDev = getUSB();
                            if(!data->dlDev)
                                data->dlDev = NUSDEV_MLC;
                            else
                                toUSB = true;
                            break;
                    }
                }
                else
                {
                    switch((int)data->dlDev)
                    {
                        case NUSDEV_USB01:
                        case NUSDEV_USB02:
                            data->dlDev = NUSDEV_SD;
                            break;
                        case NUSDEV_SD:
                            data->dlDev = NUSDEV_MLC;
                            break;
                        case NUSDEV_MLC:
                            data->dlDev = getUSB();
                            if(!data->dlDev)
                                data->dlDev = NUSDEV_SD;
                            else
                                toUSB = true;
                            break;
                    }
                }
                setDlToUSB(toUSB);
                break;
            }
            case 18:
                if(data->dlDev == NUSDEV_SD && data->operation == OPERATION_DOWNLOAD_INSTALL)
                    data->keepFiles = !data->keepFiles;
                break;
            case 19:
                showKeyboard(KEYBOARD_LAYOUT_TID, KEYBOARD_TYPE_RESTRICTED, CHECK_NUMERICAL, 5, false, data->titleVer, NULL, titleVerCallback, self);
                break;
            case 20:
                showKeyboard(KEYBOARD_LAYOUT_TID, KEYBOARD_TYPE_NORMAL, CHECK_ALPHANUMERICAL, FS_MAX_PATH - 11, false, data->folderName, NULL, folderNameCallback, self);
                break;
        }

        self->dirty = true;
    }
    else if(vpad.trigger & VPAD_BUTTON_DOWN)
    {
        if(++data->cursorPos == 21)
            data->cursorPos = 15;
        self->dirty = true;
    }
    else if(vpad.trigger & VPAD_BUTTON_UP)
    {
        if(--data->cursorPos == 14)
            data->cursorPos = 20;
        self->dirty = true;
    }

    bool start = (vpad.trigger & VPAD_BUTTON_PLUS);
    bool toQueue = (vpad.trigger & VPAD_BUTTON_MINUS);

    if(start || toQueue)
    {
        if(data->dlDev == NUSDEV_MLC)
        {
            char txt[512];
            sprintf(txt, "%s\n\n" BUTTON_A " %s || " BUTTON_B " %s", localise("Downloading to NAND is dangerous,\nit could brick your Wii U!\n\nAre you sure you want to do this?"), localise("Yes"), localise("No"));
            showConfirmation(txt, startOpCallback, data);
            return;
        }

        if(toQueue)
        {
            if(addToOpQueue(data))
                screenPop();
        }
        else
        {
            startOpCallback(true, data);
        }
        return;
    }

    if(data->installed && vpad.trigger & VPAD_BUTTON_Y)
    {
        checkSystemTitleFromListType(&data->titleList, true, deinstallCallback, data);
    }
}

static void predownloadDraw(Screen *self)
{
    PDData *data = (PDData *)self->data;
    if(data->state == 1)
    {
        startNewFrame();
        textToFrame(0, 0, localise("Downloading TMD..."));
        drawFrame();
    }
    else
    {
        drawPDMenuFrame(data);
    }
}

static void predownloadExit(Screen *self)
{
    PDData *data = (PDData *)self->data;
    if(data)
    {
        if(data->rambuf)
            freeRamBuf(data->rambuf);
        MEMFreeToDefaultHeap(data);
    }
    MEMFreeToDefaultHeap(self);
}

Screen *predownloadScreenGet(const TitleEntry *entry)
{
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    if(self == NULL)
        return NULL;

    PDData *data = MEMAllocFromDefaultHeap(sizeof(PDData));
    if(data == NULL)
    {
        MEMFreeToDefaultHeap(self);
        return NULL;
    }

    OSBlockSet(data, 0, sizeof(PDData));
    data->entry = entry;
    data->installed = isInstalled(entry, &data->titleList);
    data->cursorPos = 15;
    data->operation = OPERATION_DOWNLOAD_INSTALL;
    data->keepFiles = true;

    NUSDEV usbMounted = getUSB();
    data->dlDev = usbMounted && dlToUSBenabled() ? usbMounted : NUSDEV_SD;
    data->instDev = usbMounted ? usbMounted : NUSDEV_MLC;
    data->state = 0; // Needs initialization

    self->onUpdate = predownloadUpdate;
    self->onDraw = predownloadDraw;
    self->onExit = predownloadExit;
    self->data = data;
    self->dirty = true;

    downloadTMD(self);

    return self;
}

bool predownloadMenu(const TitleEntry *entry)
{
    Screen *s = predownloadScreenGet(entry);
    if(s)
    {
        screenPush(s);
        return true;
    }
    return false;
}
