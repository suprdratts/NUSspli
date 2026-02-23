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

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <deinstaller.h>
#include <file.h>
#include <input.h>
#include <localisation.h>
#include <menu/insttitlebrowser.h>
#include <menu/utils.h>
#include <osdefs.h>
#include <renderer.h>
#include <screen.h>
#include <state.h>
#include <staticMem.h>
#include <thread.h>
#include <titles.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <nn/acp/title.h>
#pragma GCC diagnostic pop

#define MAX_ITITLEBROWSER_LINES        (MAX_LINES - 3)
#define MAX_ITITLEBROWSER_TITLE_LENGTH (MAX_TITLENAME_LENGTH >> 1)
#define DPAD_COOLDOWN_FRAMES           30

typedef struct
{
    char name[MAX_ITITLEBROWSER_TITLE_LENGTH];
    MCPRegion region;
    bool isDlc;
    bool isUpdate;
    DEVICE_TYPE dt;
    spinlock lock;
    bool ready;
} INST_META;

typedef enum
{
    ASYNC_STATE_EXIT = 0,
    ASYNC_STATE_FWD,
    ASYNC_STATE_BKWD
} ASYNC_STATE;

typedef struct
{
    OSThread *bgt;
    INST_META *installedTitles;
    MCPTitleListType *ititleEntries;
    size_t ititleEntrySize;
    volatile ASYNC_STATE asyncState;
    size_t cursor;
    size_t pos;
    uint32_t oldHold;
    size_t frameCount;
} ITBData;

static volatile INST_META *getInstalledTitle(ITBData *data, size_t index, bool block)
{
    volatile INST_META *title = data->installedTitles + index;
    if(title->ready)
        return title;
    if(block)
        spinLock(title->lock);
    else if(!spinTryLock(title->lock))
        return NULL;
    if(!title->ready)
    {
        MCPTitleListType *list = data->ititleEntries + index;
        switch(list->indexedDevice[0])
        {
            case 'u':
                title->dt = DEVICE_TYPE_USB;
                break;
            case 'm':
                title->dt = DEVICE_TYPE_NAND;
                break;
            default:
                title->dt = DEVICE_TYPE_UNKNOWN;
        }
        const TitleEntry *e = getTitleEntryByTid(list->titleId);
        if(e)
        {
            strncpy((char *)title->name, e->name, MAX_ITITLEBROWSER_TITLE_LENGTH - 1);
            title->name[MAX_ITITLEBROWSER_TITLE_LENGTH - 1] = '\0';
            title->region = e->region;
            title->isDlc = isDLC(list->titleId);
            title->isUpdate = isUpdate(list->titleId);
        }
        else
        {
            ACPMetaXml meta __attribute__((__aligned__(0x40)));
            if(ACPGetTitleMetaXmlByTitleListType(list, &meta) == ACP_RESULT_SUCCESS && strcmp(meta.longname_en, "Long Title Name (EN)"))
            {
                strncpy((char *)title->name, meta.longname_en, MAX_ITITLEBROWSER_TITLE_LENGTH - 1);
                title->name[MAX_ITITLEBROWSER_TITLE_LENGTH - 1] = '\0';
                title->region = meta.region;
            }
            else
                hex(list->titleId, 16, (char *)title->name);
        }
        title->ready = true;
    }
    spinReleaseLock(title->lock);
    return title;
}

static int asyncTitleLoader(int argc, const char **argv)
{
    (void)argc;
    ITBData *data = (ITBData *)argv;
    size_t min = MAX_ITITLEBROWSER_LINES >> 1;
    size_t max = data->ititleEntrySize - 1;
    while(min <= max && AppRunning(false))
    {
        size_t cur;
        if(data->asyncState == ASYNC_STATE_FWD)
            cur = min++;
        else if(data->asyncState == ASYNC_STATE_BKWD)
            cur = max--;
        else
            break;
        getInstalledTitle(data, cur, false);
    }
    return 0;
}

static void drawITBMenuFrame(ITBData *data)
{
    startNewFrame();
    boxToFrame(0, MAX_LINES - 2);
    char *toFrame = getToFrameBuffer();
    strcpy(toFrame, localise("Press " BUTTON_PLUS " to launch"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_MINUS " to delete"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_B " to return"));
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, toFrame);
    size_t max = data->ititleEntrySize - data->pos;
    if(max > MAX_ITITLEBROWSER_LINES)
        max = MAX_ITITLEBROWSER_LINES;
    for(size_t i = 0, l = 1; i < max; ++i, ++l)
    {
        volatile INST_META *im = getInstalledTitle(data, data->pos + i, true);
        if(im->isDlc)
            strcpy(toFrame, "[DLC] ");
        else if(im->isUpdate)
            strcpy(toFrame, "[UPD] ");
        else
            toFrame[0] = '\0';
        if(data->cursor == i)
            arrowToFrame(l, 1);
        deviceToFrame(l, 4, im->dt);
        flagToFrame(l, 7, im->region);
        strcat(toFrame, (const char *)im->name);
        textToFrameCut(l, 10, toFrame, (SCREEN_WIDTH - (FONT_SIZE << 1)) - (getSpaceWidth() * 11));
    }
    drawFrame();
}

static void ititleBrowserRefreshCallback(bool result, void *userdata)
{
    (void)result;
    (void)userdata;
    ititleBrowserMenu();
}

static void deinstallConfirmCallback(bool result, void *userdata)
{
    ITBData *data = (ITBData *)userdata;
    if(result)
    {
        volatile INST_META *im = data->installedTitles + data->cursor + data->pos;
        MCPTitleListType *entry = data->ititleEntries + data->cursor + data->pos;

        MCPTitleListType entryCopy;
        memcpy(&entryCopy, entry, sizeof(MCPTitleListType));
        char nameCopy[256];
        strncpy(nameCopy, (const char *)im->name, 255);

        screenPop();
        deinstall(&entryCopy, nameCopy, false, false, ititleBrowserRefreshCallback, NULL);
    }
}

static void ititleBrowserUpdate(Screen *self)
{
    ITBData *data = (ITBData *)self->data;
    bool mov = data->ititleEntrySize > MAX_ITITLEBROWSER_LINES;
    if(vpad.trigger & VPAD_BUTTON_PLUS)
    {
        launchTitle(data->ititleEntries + data->cursor + data->pos);
        screenPop();
        return;
    }
    if(vpad.trigger & VPAD_BUTTON_MINUS)
    {
        volatile INST_META *im = data->installedTitles + data->cursor + data->pos;
        char toFrame[512];
        sprintf(toFrame, "%s\n%s\n%s %s drive?\n\n" BUTTON_A " %s || " BUTTON_B " %s", localise("Do you really want to uninstall"), (char *)im->name, localise("from your"), im->dt == DEVICE_TYPE_USB ? "USB" : "NAND", localise("Yes"), localise("No"));
        showConfirmation(toFrame, deinstallConfirmCallback, data);
        return;
    }
    if(vpad.trigger & VPAD_BUTTON_B)
    {
        screenPop();
        return;
    }
    if(vpad.hold & VPAD_BUTTON_UP)
    {
        if(data->oldHold != VPAD_BUTTON_UP)
        {
            data->asyncState = ASYNC_STATE_BKWD;
            data->oldHold = VPAD_BUTTON_UP;
            data->frameCount = 30;
        }
        else if(data->frameCount)
            data->frameCount--;
        if(data->frameCount == 0 || data->oldHold != VPAD_BUTTON_UP)
        {
            if(data->cursor)
                data->cursor--;
            else if(mov && data->pos)
                data->pos--;
            else if(!mov)
                data->cursor = data->ititleEntrySize - 1;
            else
            {
                data->cursor = MAX_ITITLEBROWSER_LINES - 1;
                data->pos = data->ititleEntrySize - MAX_ITITLEBROWSER_LINES;
            }
            self->dirty = true;
        }
    }
    else if(vpad.hold & VPAD_BUTTON_DOWN)
    {
        if(data->oldHold != VPAD_BUTTON_DOWN)
        {
            data->asyncState = ASYNC_STATE_FWD;
            data->oldHold = VPAD_BUTTON_DOWN;
            data->frameCount = 30;
        }
        else if(data->frameCount)
            data->frameCount--;
        if(data->frameCount == 0 || data->oldHold != VPAD_BUTTON_DOWN)
        {
            if(data->cursor + data->pos >= data->ititleEntrySize - 1)
            {
                data->cursor = data->pos = 0;
            }
            else if(data->cursor < MAX_ITITLEBROWSER_LINES - 1)
                data->cursor++;
            else
                data->pos++;
            self->dirty = true;
        }
    }
    if(data->oldHold && !(vpad.hold & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN)))
        data->oldHold = 0;
}

static void ititleBrowserDraw(Screen *self) { drawITBMenuFrame((ITBData *)self->data); }
static void ititleBrowserExit(Screen *self)
{
    ITBData *data = (ITBData *)self->data;
    if(data)
    {
        data->asyncState = ASYNC_STATE_EXIT;
        if(data->bgt)
            stopThread(data->bgt, NULL);
        if(data->ititleEntries)
            MEMFreeToDefaultHeap(data->ititleEntries);
        if(data->installedTitles)
            MEMFreeToDefaultHeap(data->installedTitles);
        MEMFreeToDefaultHeap(data);
    }
    MEMFreeToDefaultHeap(self);
}

Screen *ititleBrowserScreenGet()
{
    int32_t r = MCP_TitleCount(mcpHandle);
    if(r <= 0)
        return NULL;
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    ITBData *data = MEMAllocFromDefaultHeap(sizeof(ITBData));
    OSBlockSet(data, 0, sizeof(ITBData));
    uint32_t s = sizeof(MCPTitleListType) * (uint32_t)r;
    data->ititleEntries = (MCPTitleListType *)MEMAllocFromDefaultHeapEx(s, 0x40);
    MCP_TitleList(mcpHandle, &s, data->ititleEntries, s);
    data->ititleEntrySize = s;
    data->installedTitles = (INST_META *)MEMAllocFromDefaultHeap(s * sizeof(INST_META));
    for(size_t i = 0; i < s; ++i)
    {
        spinCreateLock(data->installedTitles[i].lock, SPINLOCK_FREE);
        data->installedTitles[i].ready = false;
    }
    data->asyncState = ASYNC_STATE_FWD;
    data->bgt = startThread("NUSspli title loader", THREAD_PRIORITY_MEDIUM, STACKSIZE_MEDIUM, asyncTitleLoader, 0, (const char **)data, OS_THREAD_ATTRIB_AFFINITY_CPU0);
    self->onUpdate = ititleBrowserUpdate;
    self->onDraw = ititleBrowserDraw;
    self->onExit = ititleBrowserExit;
    self->data = data;
    self->dirty = true;
    return self;
}

void ititleBrowserMenu()
{
    Screen *s = ititleBrowserScreenGet();
    if(s)
        screenPush(s);
}
