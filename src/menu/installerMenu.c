/***************************************************************************
 * This file is part of NUSspli.                                           *
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

#include <string.h>
#include <stdlib.h>

#include <config.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <installer.h>
#include <localisation.h>
#include <menu/filebrowser.h>
#include <menu/installer.h>
#include <menu/utils.h>
#include <queue.h>
#include <renderer.h>
#include <screen.h>
#include <state.h>
#include <tmd.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

typedef struct
{
    char dir[FS_MAX_PATH];
    TMD *tmd;
    const TitleEntry *entry;
    NUSDEV dev;
    NUSDEV toDev;
    bool usbMounted;
    bool keepFiles;
    int cursorPos;
} InstallerData;

static bool addToOpQueue(const TitleEntry *entry, const char *dir, const TMD *tmd, NUSDEV fromDev, bool toUSB, bool keepFiles)
{
    TitleData *titleInfo = MEMAllocFromDefaultHeap(sizeof(TitleData));
    if(titleInfo == NULL)
        return false;

    titleInfo->tmd = (TMD *)tmd;
    titleInfo->rambuf = NULL;
    titleInfo->operation = OPERATION_INSTALL;
    titleInfo->entry = entry;
    strcpy(titleInfo->folderName, dir);
    titleInfo->dlDev = fromDev;
    titleInfo->toUSB = toUSB;
    titleInfo->keepFiles = keepFiles;

    int ret = addToQueue(titleInfo);
    if(ret == 1)
        return true;

    MEMFreeToDefaultHeap(titleInfo);
    return ret;
}

static void drawInstallerMenuFrame(const InstallerData *data)
{
    startNewFrame();
    textToFrame(0, 0, localise("Name:"));

    char *toFrame = getToFrameBuffer();
    const char *nd = data->entry == NULL ? prettyDir(data->dir) : data->entry->name;
    strcpy(toFrame, nd);
    char tid[17];
    hex(data->tmd->tid, 16, tid);
    strcat(toFrame, " [");
    strcat(toFrame, tid);
    strcat(toFrame, "]");
    int line = textToFrameMultiline(0, ALIGNED_CENTER, toFrame, MAX_CHARS - 33);

    uint64_t size = 0;
    for(uint16_t i = 0; i < data->tmd->num_contents; ++i)
        size += data->tmd->contents[i].size;

    humanize(size, toFrame);

    textToFrame(++line, 0, localise("Region:"));
    MCPRegion region = data->entry == NULL ? MCP_REGION_UNKNOWN : data->entry->region;
    flagToFrame(++line, 3, region);
    textToFrame(line, 7, localise(getFormattedRegion(region)));

    textToFrame(++line, 0, localise("Size:"));
    textToFrame(++line, 3, toFrame);

    lineToFrame(MAX_LINES - 6, SCREEN_COLOR_WHITE);
    arrowToFrame(data->cursorPos, 0);

    strcpy(toFrame, localise("Install to:"));
    strcat(toFrame, " ");
    switch((int)data->toDev)
    {
        case NUSDEV_USB01:
        case NUSDEV_USB02:
            strcat(toFrame, "USB");
            break;
        case NUSDEV_MLC:
            strcat(toFrame, "NAND");
            break;
    }

    getFreeSpaceString(data->toDev, toFrame + strlen(toFrame));

    if(data->usbMounted)
        textToFrame(MAX_LINES - 5, 4, toFrame);
    else
        textToFrameColored(MAX_LINES - 5, 4, toFrame, SCREEN_COLOR_WHITE_TRANSP);

    strcpy(toFrame, localise("Keep downloaded files:"));
    strcat(toFrame, " ");
    strcat(toFrame, localise(data->keepFiles ? "Yes" : "No"));
    if(data->dev == NUSDEV_SD)
        textToFrame(MAX_LINES - 4, 4, localise(toFrame));
    else
        textToFrameColored(MAX_LINES - 4, 4, localise(toFrame), SCREEN_COLOR_WHITE_TRANSP);

    lineToFrame(MAX_LINES - 3, SCREEN_COLOR_WHITE);

    strcpy(toFrame, localise("Press " BUTTON_B " to return"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_PLUS " to start"));
    textToFrame(MAX_LINES - 2, ALIGNED_CENTER, toFrame);

    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, localise(BUTTON_MINUS " to add to the queue"));

    drawFrame();
}

static void installResultCallback(bool result, void *userdata)
{
    const char *nd = (const char *)userdata;
    if(result)
        showFinishedScreen(nd, FINISHING_OPERATION_INSTALL);
}

static void sysCheckCallback(bool result, void *userdata)
{
    Screen *self = (Screen *)userdata;
    InstallerData *data = (InstallerData *)self->data;
    if(result)
    {
        const char *nd = data->entry == NULL ? prettyDir(data->dir) : data->entry->name;
        install(nd, false, data->dev, data->dir, data->toDev & NUSDEV_USB, data->keepFiles, data->tmd, installResultCallback, (void *)nd);
        data->tmd = NULL;
        screenPop();
    }
}

static void installerUpdate(Screen *self)
{
    InstallerData *data = (InstallerData *)self->data;

    if(vpad.trigger & VPAD_BUTTON_B)
    {
        screenPop();
        installerMenu();
        return;
    }

    if(vpad.trigger & VPAD_BUTTON_PLUS)
    {
        checkSystemTitleFromTid(data->tmd->tid, false, sysCheckCallback, self);
        return;
    }
    else if(vpad.trigger & VPAD_BUTTON_MINUS)
    {
        if(addToOpQueue(data->entry, data->dir, data->tmd, data->dev, data->toDev & NUSDEV_USB, data->keepFiles))
        {
            data->tmd = NULL;
            screenPop();
        }
        return;
    }
    else if(vpad.trigger & (VPAD_BUTTON_A | VPAD_BUTTON_RIGHT | VPAD_BUTTON_LEFT))
    {
        switch(data->cursorPos)
        {
            case MAX_LINES - 5:
                if(data->usbMounted)
                {
                    if(data->toDev & NUSDEV_USB)
                        data->toDev = NUSDEV_MLC;
                    else
                        data->toDev = (NUSDEV)getUSB();
                }
                break;
            case MAX_LINES - 4:
                if(data->dev == NUSDEV_SD)
                    data->keepFiles = !data->keepFiles;
                break;
        }

        self->dirty = true;
    }
    else if(vpad.trigger & VPAD_BUTTON_DOWN)
    {
        if(++data->cursorPos == MAX_LINES - 3)
            data->cursorPos = MAX_LINES - 5;

        self->dirty = true;
    }
    else if(vpad.trigger & VPAD_BUTTON_UP)
    {
        if(--data->cursorPos == MAX_LINES - 6)
            data->cursorPos = MAX_LINES - 4;

        self->dirty = true;
    }
}

static void installerDraw(Screen *self)
{
    drawInstallerMenuFrame((InstallerData *)self->data);
}

static void installerExit(Screen *self)
{
    InstallerData *data = (InstallerData *)self->data;
    if(data)
    {
        if(data->tmd)
            MEMFreeToDefaultHeap(data->tmd);
        MEMFreeToDefaultHeap(data);
    }
    MEMFreeToDefaultHeap(self);
}

static void fileBrowserCallback(const char *path, void *userdata)
{
    (void)userdata;
    if(path == NULL)
        return;

    TMD *tmd = getTmd(path, true);
    if(tmd == NULL)
    {
        showErrorFrame(localise("Invalid title.tmd file!"));
        installerMenu();
        return;
    }

    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    if(self == NULL)
    {
        MEMFreeToDefaultHeap(tmd);
        return;
    }

    InstallerData *data = MEMAllocFromDefaultHeap(sizeof(InstallerData));
    if(data == NULL)
    {
        MEMFreeToDefaultHeap(tmd);
        MEMFreeToDefaultHeap(self);
        return;
    }

    OSBlockSet(data, 0, sizeof(InstallerData));
    strcpy(data->dir, path);
    data->tmd = tmd;
    data->dev = getDevFromPath(path);
    data->keepFiles = data->dev == NUSDEV_SD;
    data->toDev = getUSB();
    data->usbMounted = data->toDev & NUSDEV_USB;
    if(!data->usbMounted)
        data->toDev = NUSDEV_MLC;
    data->entry = getTitleEntryByTid(tmd->tid);
    data->cursorPos = MAX_LINES - 5;

    self->onUpdate = installerUpdate;
    self->onDraw = installerDraw;
    self->onExit = installerExit;
    self->data = data;
    self->dirty = true;

    screenPush(self);
}

void installerMenu()
{
    fileBrowserMenu(true, true, fileBrowserCallback, NULL);
}
