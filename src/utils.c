/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2019-2020 Pokes303                                        *
 * Copyright (c) 2020-2022 V10lator <v10lator@myway.de>                    *
 * Copyright (c) 2022 Xpl0itU <DaThinkingChair@protonmail.com>             *
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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <crypto.h>
#include <input.h>
#include <localisation.h>
#include <menu/utils.h>
#include <renderer.h>
#include <screen.h>
#include <state.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/atomic.h>
#include <coreinit/ios.h>
#include <coreinit/memory.h>
#include <coreinit/memdefaultheap.h>
#pragma GCC diagnostic pop

int mcpHandle;

bool isNumber(char c)
{
    return c >= '0' && c <= '9';
}

bool isLowercase(char c)
{
    return c >= 'a' && c <= 'z';
}

bool isUppercase(char c)
{
    return c >= 'A' && c <= 'Z';
}

bool isAlphanumerical(char c)
{
    return isLowercase(c) || isUppercase(c) || isNumber(c);
}

// Keep it to ASCII for FTPiiU compat.
bool isAllowedInFilename(char c)
{
    return c >= ' ' && c <= '~' && c != '/' && c != '\\' && c != '"' && c != '*' && c != ':' && c != '<' && c != '>' && c != '?' && c != '|';
}

bool isLowercaseHexa(char c)
{
    return isNumber(c) || (c >= 'a' && c <= 'f');
}

bool isUppercaseHexa(char c)
{
    return isNumber(c) || (c >= 'A' && c <= 'F');
}

bool isHexa(char c)
{
    return isLowercaseHexa(c) || isUppercaseHexa(c);
}

void hex(uint64_t i, int digits, char *out)
{
    char x[8]; // max 99 digits!
    sprintf(x, "%%0%illx", digits);
    sprintf(out, x, i);
}

void getSpeedString(float bytePerSecond, char *out)
{
    float bitPerSecond = bytePerSecond * 8.0f;

    if(bitPerSecond < 1024.0f)
        sprintf(out, "%.2f b/s (", bitPerSecond);
    else if(bitPerSecond < 1024.0f * 1024.0f)
        sprintf(out, "%.2f Kb/s (", bitPerSecond / 1024.0f);
    else if(bitPerSecond < 1024.0f * 1024.0f * 1024.0f)
        sprintf(out, "%.2f Mb/s (", bitPerSecond / (1024.0f * 1024.0f));
    else
        sprintf(out, "%.2f Gb/s (", bitPerSecond / (1024.0f * 1024.0f * 1024.0f));

    out += strlen(out);

    if(bytePerSecond < 1024.0f)
        sprintf(out, "%.2f B/s)", bytePerSecond);
    else if(bytePerSecond < 1024.0f * 1024.0f)
        sprintf(out, "%.2f KB/s)", bytePerSecond / 1024.0f);
    else if(bytePerSecond < 1024.0f * 1024.0f * 1024.0f)
        sprintf(out, "%.2f MB/s)", bytePerSecond / (1024.0f * 1024.0f));
    else
        sprintf(out, "%.2f GB/s)", bytePerSecond / (1024.0f * 1024.0f * 1024.0f));
}

void secsToTime(uint32_t seconds, char *out)
{
    uint32_t days = seconds / 86400;
    uint32_t hours;
    uint32_t minutes;
    if(days < 100)
    {
        hours = (seconds % 86400) / 3600;
        minutes = (seconds % 3600) / 60;
        seconds %= 60;
    }
    else
    {
        days = 99;
        hours = 23;
        minutes = 59;
        seconds = 59;
    }

    bool visible = false;

    if(days)
    {
        sprintf(out, "%u %s ", days, localise("days"));
        out += strlen(out);
        visible = true;
    }
    if(hours || visible)
    {
        sprintf(out, visible ? "%02u %s " : "%u %s ", hours, localise("hours"));
        out += strlen(out);
        visible = true;
    }
    if(minutes || visible)
    {
        sprintf(out, visible ? "%02u %s " : "%u %s ", minutes, localise("minutes"));
        out += strlen(out);
        visible = true;
    }

    sprintf(out, visible ? "%02u %s" : "%u %s", seconds, localise("seconds"));
}

uint8_t charToByte(char c)
{
    if(isNumber(c))
        return c - '0';
    if(isLowercaseHexa(c))
        return c - 'a' + 0xA;
    if(isUppercaseHexa(c))
        return c - 'A' + 0xA;
    return 0xFF;
}

void hexToByte(const char *hex, uint8_t *out)
{
    for(int i = 0; *hex != '\0' && i < 64; out[i++] |= charToByte(*hex++))
        out[i] = charToByte(*hex++) << 4;
}

static void mcpCallback(MCPError err, void *rawData)
{
    McpData *data = (McpData *)rawData;
    if(data->err == 0)
        data->err = err;
    data->processing = false;
}

void glueMcpData(MCPInstallTitleInfo *info, McpData *data)
{
    data->processing = true;
    data->err = 0;
    uint32_t *ptr = (uint32_t *)info;
    *ptr = (uint32_t)mcpCallback;
    *++ptr = (uint32_t)data;
}

typedef struct
{
    McpData *mcp_data;
    char *game;
    bool inst;
    void *ovl;
    MCPInstallProgress progress;
    OSTime lastSpeedCalc;
    uint64_t lsp;
    char speedBuf[32];
    ResultCallback callback;
    void *userdata;
} McpProgressData;

static void mcpProgressUpdate(Screen *self)
{
    McpProgressData *data = (McpProgressData *)self->data;

    if(!data->mcp_data->processing)
    {
        screenPop();
        if(data->callback) data->callback(data->mcp_data->err == 0, data->userdata);
        return;
    }

    MCPError err = MCP_InstallGetProgress(mcpHandle, &data->progress);
    if(err == IOS_ERROR_OK)
    {
        if(data->progress.inProgress == 1 && data->progress.sizeTotal != 0 && data->mcp_data->err != CUSTOM_MCP_ERROR_CANCELLED)
        {
            self->dirty = true;
        }
    }

    if(data->inst)
    {
        if(data->ovl == NULL)
        {
            if(vpad.trigger & VPAD_BUTTON_B)
            {
                char toScreen[512];
                sprintf(toScreen, "%s\n\n" BUTTON_A " %s || " BUTTON_B " %s", localise("Do you really want to cancel?"), localise("Yes"), localise("No"));
                data->ovl = addErrorOverlay(toScreen);
            }
        }
        else
        {
            if(vpad.trigger & VPAD_BUTTON_A)
            {
                removeErrorOverlay(data->ovl);
                data->ovl = NULL;
                data->inst = false;

                startNewFrame();
                textToFrame(0, 0, localise("Cancelling installation."));
                textToFrame(1, 0, localise("Please wait..."));
                writeScreenLog(2);
                drawFrame();

                MCP_InstallTitleAbort(mcpHandle);
                data->mcp_data->err = CUSTOM_MCP_ERROR_CANCELLED;
            }
            else if(vpad.trigger & VPAD_BUTTON_B)
            {
                removeErrorOverlay(data->ovl);
                data->ovl = NULL;
            }
        }
    }
}

static void mcpProgressDraw(Screen *self)
{
    McpProgressData *data = (McpProgressData *)self->data;
    char *toScreen = getToFrameBuffer();

    if(data->progress.inProgress == 1 && data->progress.sizeTotal != 0 && data->mcp_data->err != CUSTOM_MCP_ERROR_CANCELLED)
    {
        startNewFrame();
        strcpy(toScreen, localise(data->inst ? "Installing" : "Uninstalling"));
        strcat(toScreen, " ");
        strcat(toScreen, data->game);
        textToFrame(0, 0, toScreen);
        barToFrame(1, 0, 40, (float)data->progress.sizeProgress / (float)data->progress.sizeTotal);
        humanize(data->progress.sizeProgress, toScreen);
        strcat(toScreen, " / ");
        humanize(data->progress.sizeTotal, toScreen + strlen(toScreen));
        textToFrame(1, 41, toScreen);

        if(data->progress.sizeProgress != 0)
        {
            OSTime now = OSGetSystemTime();
            if(OSTicksToMilliseconds(now - data->lastSpeedCalc) > 333)
            {
                getSpeedString(data->progress.sizeProgress - data->lsp, data->speedBuf);
                data->lsp = data->progress.sizeProgress;
                data->lastSpeedCalc = now;
            }
            textToFrame(1, ALIGNED_RIGHT, data->speedBuf);
        }

        writeScreenLog(2);
        drawFrame();
    }
}

static void mcpProgressExit(Screen *self)
{
    McpProgressData *data = (McpProgressData *)self->data;
    if(data)
    {
        if(data->ovl) removeErrorOverlay(data->ovl);
        if(data->game) MEMFreeToDefaultHeap(data->game);
        MEMFreeToDefaultHeap(data);
    }
    MEMFreeToDefaultHeap(self);
}

void showMcpProgress(McpData *mcp_data, const char *game, bool inst, ResultCallback callback, void *userdata)
{
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    if(self == NULL) return;

    McpProgressData *data = MEMAllocFromDefaultHeap(sizeof(McpProgressData));
    if(data == NULL) { MEMFreeToDefaultHeap(self); return; }

    OSBlockSet(data, 0, sizeof(McpProgressData));
    data->mcp_data = mcp_data;
    if(game)
    {
        data->game = MEMAllocFromDefaultHeap(strlen(game) + 1);
        if(data->game) strcpy(data->game, game);
    }
    else
        data->game = NULL;
    data->inst = inst;
    data->callback = callback;
    data->userdata = userdata;

    self->onUpdate = mcpProgressUpdate;
    self->onDraw = mcpProgressDraw;
    self->onExit = mcpProgressExit;
    self->data = data;
    self->dirty = true;

    screenPush(self);
}

#ifdef NUSSPLI_DEBUG

#include <stdarg.h>

#include <thread.h>

#include <coreinit/fastmutex.h>
#include <coreinit/time.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>

static const char days[7][4] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};

static const char months[12][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Nov", "Dez",
};

static spinlock debugLock;

void debugInit()
{
    spinCreateLock(debugLock, SPINLOCK_FREE);
    WHBLogUdpInit();
    WHBLogCafeInit();
}

void shutdownDebug()
{
    WHBLogUdpDeinit();
    WHBLogCafeDeinit();
}

void restartUdpLog1()
{
    spinLock(debugLock);
    WHBLogUdpDeinit();
}

void restartUdpLog2()
{
    WHBLogUdpInit();
    spinReleaseLock(debugLock);
}

void debugPrintf(const char *str, ...)
{
    spinLock(debugLock);
    static char newStr[512];
    OSCalendarTime now;
    OSTicksToCalendarTime(OSGetTime(), &now);
    sprintf(newStr, "%s %02d %s %d %02d:%02d:%02d.%03d\t", days[now.tm_wday], now.tm_mday, months[now.tm_mon], now.tm_year, now.tm_hour, now.tm_min, now.tm_sec, now.tm_msec);
    size_t tss = strlen(newStr);
    va_list va;
    va_start(va, str);
    vsnprintf(newStr + tss, 511 - tss, str, va);
    va_end(va);
    WHBLogPrint(newStr);
    spinReleaseLock(debugLock);
}

void checkStacks(const char *src)
{
    debugPrintf("%s: Checking thread stacks...", src);
    OSCheckActiveThreads();
    OSThread *trd = OSGetCurrentThread();
    debugPrintf("%s: 0x%08X/0x%08X", src, OSCheckThreadStackUsage(trd), ((uint32_t)trd->stackStart) - ((uint32_t)trd->stackEnd));
}

#endif
