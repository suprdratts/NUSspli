/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2021-2022 V10lator <v10lator@myway.de>                    *
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
#include <stdlib.h>
#include <string.h>

#include <crypto.h>
#include <deinstaller.h>
#include <filesystem.h>
#include <localisation.h>
#include <menu/utils.h>
#include <osdefs.h>
#include <renderer.h>
#include <screen.h>
#include <state.h>
#include <ticket.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/mcp.h>
#include <coreinit/thread.h>
#include <coreinit/memdefaultheap.h>
#pragma GCC diagnostic pop

typedef struct
{
    MCPTitleListType title;
    char *name;
    bool channelHaxx;
    bool skipEnd;
    ResultCallback callback;
    void *userdata;
    int state;
    McpData mcp_data;
    size_t titleSize;
    int timer;
} DeinstallData;

static void deinstallDoneCallback(bool result, void *userdata)
{
    DeinstallData *data = (DeinstallData *)userdata;
    ResultCallback cb = data->callback;
    void *ud = data->userdata;
    MCPTitleListType title = data->title;
    char *name = data->name;
    bool channelHaxx = data->channelHaxx;
    bool skipEnd = data->skipEnd;
    size_t titleSize = data->titleSize;

    data->name = NULL; // Hand over ownership
    screenPop(); // Pop deinstall screen

    if(result)
    {
        deleteTicket(title.titleId);
        if(!channelHaxx) enableShutdown();
        freeSpace(getDevFromPath(title.path), titleSize);
        addToScreenLog("Deinstallation finished!");
        if(!skipEnd) showFinishedScreen(name, FINISHING_OPERATION_DEINSTALL);
    }
    else
    {
        if(!channelHaxx) enableShutdown();
    }
    if(cb) cb(result, ud);
    if(name) MEMFreeToDefaultHeap(name);
}

static void deinstallUpdate(Screen *self)
{
    DeinstallData *data = (DeinstallData *)self->data;

    switch(data->state)
    {
        case 0:
        {
            data->titleSize = getDirsize(data->title.path);
            MCPInstallTitleInfo info __attribute__((__aligned__(0x40)));
            glueMcpData(&info, &data->mcp_data);

            debugPrintf("Deleting %s", data->title.path);
            if(!data->channelHaxx) disableShutdown();

            MCPError err = MCP_DeleteTitleAsync(mcpHandle, data->title.path, &info);
            if(err != 0)
            {
                debugPrintf("Err1: %#010x (%d)", err, err);
                if(!data->channelHaxx) enableShutdown();
                screenPop();
                if(data->callback) data->callback(false, data->userdata);
                return;
            }

            if(data->channelHaxx)
            {
                data->state = 3;
                data->timer = 60 * 10;
                break;
            }

            data->state = 1;
            showMcpProgress(&data->mcp_data, data->name, false, deinstallDoneCallback, data);
            break;
        }
        case 3:
        {
            if(--data->timer == 0)
            {
                ResultCallback cb = data->callback;
                void *ud = data->userdata;
                screenPop();
                if(cb) cb(true, ud);
            }
            break;
        }
        default: break;
    }
}

static void deinstallDraw(Screen *self)
{
    DeinstallData *data = (DeinstallData *)self->data;
    startNewFrame();
    char *toFrame = getToFrameBuffer();
    strcpy(toFrame, localise("Uninstalling"));
    strcat(toFrame, " ");
    strcat(toFrame, data->name);
    textToFrame(0, 0, toFrame);
    textToFrame(1, 0, localise("Preparing..."));
    drawFrame();
}

static void deinstallExit(Screen *self)
{
    DeinstallData *data = (DeinstallData *)self->data;
    if(data)
    {
        if(data->name) MEMFreeToDefaultHeap(data->name);
        MEMFreeToDefaultHeap(data);
    }
    MEMFreeToDefaultHeap(self);
}

void deinstall(MCPTitleListType *title, const char *name, bool channelHaxx, bool skipEnd, ResultCallback callback, void *userdata)
{
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    if(self == NULL) return;
    DeinstallData *data = MEMAllocFromDefaultHeap(sizeof(DeinstallData));
    if(data == NULL) { MEMFreeToDefaultHeap(self); return; }
    OSBlockSet(data, 0, sizeof(DeinstallData));
    data->title = *title;
    data->name = MEMAllocFromDefaultHeap(strlen(name) + 1); if(data->name) strcpy(data->name, name);
    data->channelHaxx = channelHaxx; data->skipEnd = skipEnd; data->callback = callback; data->userdata = userdata; data->state = 0;
    self->onUpdate = deinstallUpdate; self->onDraw = deinstallDraw; self->onExit = deinstallExit; self->data = data; self->dirty = true;
    screenPush(self);
}
