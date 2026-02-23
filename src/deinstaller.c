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
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#pragma GCC diagnostic pop

typedef struct
{
    char name[256];
    bool channelHaxx;
    bool skipEnd;
    uint64_t tid;
    char path[FS_MAX_PATH];
    size_t titleSize;
    McpData mcp_data;
    ResultCallback callback;
    void *userdata;
} DeinstallData;

static void deinstallProgressCallback(bool result, void *userdata)
{
    DeinstallData *data = (DeinstallData *)userdata;

    if(!data->channelHaxx)
    {
        deleteTicket(data->tid);
        enableShutdown();
    }

    freeSpace(getDevFromPath(data->path), data->titleSize);
    addToScreenLog("Deinstallation finished!");

    if(!data->skipEnd)
        showFinishedScreen(data->name, FINISHING_OPERATION_DEINSTALL);

    if(data->callback)
        data->callback(result, data->userdata);
    MEMFreeToDefaultHeap(data);
}

void deinstall(MCPTitleListType *title, const char *name, bool channelHaxx, bool skipEnd, ResultCallback callback, void *userdata)
{
    DeinstallData *data = MEMAllocFromDefaultHeap(sizeof(DeinstallData));
    if(data == NULL)
    {
        if(callback)
            callback(false, userdata);
        return;
    }

    OSBlockSet(data, 0, sizeof(DeinstallData));
    strncpy(data->name, name, 255);
    data->channelHaxx = channelHaxx;
    data->skipEnd = skipEnd;
    data->tid = title->titleId;
    strncpy(data->path, title->path, FS_MAX_PATH - 1);
    data->titleSize = getDirsize(title->path);
    data->callback = callback;
    data->userdata = userdata;

    startNewFrame();
    char *toFrame = getToFrameBuffer();
    strcpy(toFrame, localise("Uninstalling"));
    strcat(toFrame, " ");
    strcat(toFrame, name);
    textToFrame(0, 0, toFrame);
    textToFrame(1, 0, localise("Preparing..."));
    writeScreenLog(2);
    drawFrame();
    showFrame();

    MCPInstallTitleInfo info __attribute__((__aligned__(0x40)));
    glueMcpData(&info, &data->mcp_data);

    if(!channelHaxx)
        disableShutdown();

    MCPError err = MCP_DeleteTitleAsync(mcpHandle, title->path, &info);
    if(err != 0)
    {
        if(!channelHaxx)
            enableShutdown();
        if(callback)
            callback(false, userdata);
        MEMFreeToDefaultHeap(data);
        return;
    }

    if(channelHaxx)
    {
        if(callback)
            callback(true, userdata);
        MEMFreeToDefaultHeap(data);
        return;
    }

    showMcpProgress(&data->mcp_data, name, false, deinstallProgressCallback, data);
}
