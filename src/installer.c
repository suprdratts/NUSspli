/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2022 V10lator <v10lator@myway.de>                    *
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <crypto.h>
#include <deinstaller.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <installer.h>
#include <ioQueue.h>
#include <localisation.h>
#include <menu/utils.h>
#include <no-intro.h>
#include <renderer.h>
#include <screen.h>
#include <state.h>
#include <staticMem.h>
#include <ticket.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/mcp.h>
#include <coreinit/memory.h>
#include <coreinit/time.h>
#include <coreinit/memdefaultheap.h>
#pragma GCC diagnostic pop

#define IMPORTDIR_USB1 (NUSDIR_USB1 "usr/import/")
#define IMPORTDIR_USB2 (NUSDIR_USB2 "usr/import/")
#define IMPORTDIR_MLC  (NUSDIR_MLC "usr/import/")

static void cleanupCancelledInstallation(NUSDEV dev, const char *path, bool toUsb, bool keepFiles)
{
    debugPrintf("Cleaning up...");

    switch(dev)
    {
        case NUSDEV_USB01:
        case NUSDEV_USB02:
        case NUSDEV_MLC:
            keepFiles = false;
        default:
            break;
    }

    if(!keepFiles)
        removeDirectory(path);

    FSADirectoryHandle dir;
    char importPath[sizeof(IMPORTDIR_MLC) + 8];
    OSBlockMove(importPath, toUsb ? (getUSB() == NUSDEV_USB01 ? IMPORTDIR_USB1 : IMPORTDIR_USB2) : IMPORTDIR_MLC, sizeof(IMPORTDIR_MLC), false);

    if(FSAOpenDir(getFSAClient(), importPath, &dir) == FS_ERROR_OK)
    {
        importPath[sizeof(IMPORTDIR_MLC) + 7] = '\0';
        FSADirectoryEntry entry;

        while(FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
        {
            if(!(entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 8)
                continue;

            OSBlockMove(importPath + (sizeof(IMPORTDIR_MLC) - 1), entry.name, 8, false);
            removeDirectory(importPath);
        }

        FSACloseDir(getFSAClient(), dir);
    }
}

typedef struct
{
    char *game;
    bool hasDeps;
    NUSDEV dev;
    char *path;
    bool toUsb;
    bool keepFiles;
    TMD *tmd;
    ResultCallback callback;
    void *userdata;
    int state;
    McpData mcp_data;
    NO_INTRO_DATA *noIntro;
    uint64_t size;
} InstallData;

static void installDoneCallback(bool result, void *userdata)
{
    InstallData *data = (InstallData *)userdata;
    ResultCallback cb = data->callback;
    void *ud = data->userdata;
    screenPop(); // Pop install screen
    if(cb) cb(result, ud);
}

static void deinstallDone(bool result, void *userdata)
{
    (void)result;
    InstallData *data = (InstallData *)userdata;
    data->state = 1; // Proceed to installation
}

static void noIntroDone(NO_INTRO_DATA *noIntro, void *userdata)
{
    InstallData *data = (InstallData *)userdata;
    data->noIntro = noIntro;
    data->state = 11; // Proceed after no-intro
}

static void installUpdate(Screen *self)
{
    InstallData *data = (InstallData *)self->data;

    switch(data->state)
    {
        case 0:
        {
            if(data->tmd != NULL)
            {
                MCPTitleListType titleEntry __attribute__((__aligned__(0x40)));
                if(MCP_GetTitleInfo(mcpHandle, data->tmd->tid, &titleEntry) == 0)
                {
                    data->state = 10; // Waiting for deinstall
                    deinstall(&titleEntry, data->game, false, true, deinstallDone, data);
                    return;
                }
            }
            data->state = 1;
            self->dirty = true;
            break;
        }
        case 1:
        {
            flushIOQueue();
            char tmpPath[FS_MAX_PATH];
            size_t s = strlen(data->path);
            strcpy(tmpPath, data->path);
            strcpy(tmpPath + s, "title.tmd");
            if(!fileExists(tmpPath))
            {
                data->state = 10; // Use 10 as generic wait
                transformNoIntro(data->path, noIntroDone, data);
                return;
            }
            data->state = 11;
            break;
        }
        case 11:
        {
            if(data->tmd == NULL)
            {
                data->tmd = getTmd(data->path, false);
                if(data->tmd == NULL)
                {
                    showErrorFrame(localise("No title.tmd found!"));
                    screenPop();
                    if(data->callback) data->callback(false, data->userdata);
                    return;
                }
            }
            data->size = 0;
            for(uint16_t i = 0; i < data->tmd->num_contents; ++i)
                data->size += data->tmd->contents[i].size;
            if(!checkFreeSpace(data->toUsb ? getUSB() : NUSDEV_MLC, data->size))
            {
                screenPop();
                if(data->callback) data->callback(false, data->userdata);
                return;
            }

            MCPInstallTitleInfo info __attribute__((__aligned__(0x40)));
            MCPError err = MCP_InstallGetInfo(mcpHandle, data->path, (MCPInstallInfo *)&info);
            if(err != 0)
            {
                if(data->noIntro) revertNoIntro(data->noIntro);
                showErrorFrame(localise("Error getting info from MCP"));
                screenPop();
                if(data->callback) data->callback(false, data->userdata);
                return;
            }
            MCPInstallTarget target = data->toUsb ? MCP_INSTALL_TARGET_USB : MCP_INSTALL_TARGET_MLC;
            err = MCP_InstallSetTargetDevice(mcpHandle, target);
            if(err == 0 && data->toUsb && getUSB() == NUSDEV_USB02)
                 err = MCP_InstallSetTargetUsb(mcpHandle, ++target);

            if(err != 0)
            {
                if(data->noIntro) revertNoIntro(data->noIntro);
                showErrorFrame(localise("Error opening target device"));
                screenPop();
                if(data->callback) data->callback(false, data->userdata);
                return;
            }
            glueMcpData(&info, &data->mcp_data);
            disableShutdown();
            err = MCP_InstallTitleAsync(mcpHandle, data->path, &info);
            if(err != 0)
            {
                enableShutdown();
                if(data->noIntro) revertNoIntro(data->noIntro);
                showErrorFrame(localise("Error starting installation"));
                screenPop();
                if(data->callback) data->callback(false, data->userdata);
                return;
            }
            data->state = 2;
            showMcpProgress(&data->mcp_data, data->game, true, installDoneCallback, data);
            break;
        }
        case 2:
        case 10:
        {
            break;
        }
    }
}

static void installDraw(Screen *self)
{
    InstallData *data = (InstallData *)self->data;
    startNewFrame();
    char *toScreen = getToFrameBuffer();
    strcpy(toScreen, localise("Installing"));
    strcat(toScreen, " ");
    strcat(toScreen, data->game);
    textToFrame(0, 0, toScreen);
    if(data->state == 1 || data->state == 11)
    {
        barToFrame(1, 0, 40, 0.0f);
        textToFrame(1, 41, localise("Preparing..."));
    }
    drawFrame();
}

static void installExit(Screen *self)
{
    InstallData *data = (InstallData *)self->data;
    if(data)
    {
        if(data->game) MEMFreeToDefaultHeap(data->game);
        if(data->path) MEMFreeToDefaultHeap(data->path);
        MEMFreeToDefaultHeap(data);
    }
    MEMFreeToDefaultHeap(self);
}

void install(const char *game, bool hasDeps, NUSDEV dev, const char *path, bool toUsb, bool keepFiles, const TMD *tmd, ResultCallback callback, void *userdata)
{
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    if(self == NULL) return;
    InstallData *data = MEMAllocFromDefaultHeap(sizeof(InstallData));
    if(data == NULL) { MEMFreeToDefaultHeap(self); return; }
    OSBlockSet(data, 0, sizeof(InstallData));
    data->game = MEMAllocFromDefaultHeap(strlen(game) + 1); if(data->game) strcpy(data->game, game);
    data->hasDeps = hasDeps; data->dev = dev;
    data->path = MEMAllocFromDefaultHeap(strlen(path) + 1); if(data->path) strcpy(data->path, path);
    data->toUsb = toUsb; data->keepFiles = keepFiles; data->tmd = (TMD *)tmd;
    data->callback = callback; data->userdata = userdata; data->state = 0;
    self->onUpdate = installUpdate; self->onDraw = installDraw; self->onExit = installExit; self->data = data; self->dirty = true;
    screenPush(self);
}
