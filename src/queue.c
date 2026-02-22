/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2022 Xpl0itU <DaThinkingChair@protonmail.com>             *
 * Copyright (c) 2022-2023 V10lator <v10lator@myway.de>                    *
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

#include <downloader.h>
#include <installer.h>
#include <list.h>
#include <menu/utils.h>
#include <queue.h>
#include <screen.h>
#include <state.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#pragma GCC diagnostic pop

static LIST *titleQueue;

bool initQueue()
{
    titleQueue = createList();
    return titleQueue != NULL;
}

void shutdownQueue()
{
    clearQueue();
    destroyList(titleQueue, false);
}

int addToQueue(TitleData *data)
{
    TitleData *title;
    forEachListEntry(titleQueue, title)
    {
        if(data->operation & OPERATION_INSTALL && title->operation & OPERATION_INSTALL)
        {
            if(data->toUSB && title->toUSB && data->tmd->tid == title->tmd->tid)
                return 2;
        }
        if(data->operation & OPERATION_DOWNLOAD && title->operation & OPERATION_DOWNLOAD)
        {
            if(data->dlDev == title->dlDev && data->tmd->tid == title->tmd->tid)
                return 3;
        }
    }

    return addToListEnd(titleQueue, data) ? 1 : 0;
}

static inline void removeFQ(TitleData *title)
{
    if(title != NULL)
    {
        removeFromList(titleQueue, title);
        if(title->rambuf != NULL)
            freeRamBuf(title->rambuf);
        else
            MEMFreeToDefaultHeap(title->tmd);

        MEMFreeToDefaultHeap(title);
    }
}

typedef struct
{
    QUEUE_DATA queueData;
    ResultCallback callback;
    void *userdata;
    int state;
} ProcessQueueData;

static void queueItemDone(bool result, void *userdata)
{
    Screen *self = (Screen *)userdata;
    ProcessQueueData *data = (ProcessQueueData *)self->data;
    if(!result)
    {
        screenPop();
        if(data->callback) data->callback(false, data->userdata);
        return;
    }
    data->state = 1; // Process next item
}

static void processQueueUpdate(Screen *self)
{
    ProcessQueueData *data = (ProcessQueueData *)self->data;
    TitleData *title = getContent(titleQueue, 0);

    if(title == NULL)
    {
        enableApd();
        screenPop();
        if(data->callback) data->callback(true, data->userdata);
        return;
    }

    switch(data->state)
    {
        case 0: // Pre-checks done, start processing
            disableApd();
            data->state = 1;
            break;
        case 1: // Start next item
            data->state = 2;
            if(title->operation & OPERATION_DOWNLOAD)
            {
                data->queueData.current++;
                downloadTitle(title->tmd, title->tmdSize, title->entry, title->titleVer, title->folderName, title->operation & OPERATION_INSTALL, title->dlDev, title->toUSB, title->keepFiles, &data->queueData, queueItemDone, self);
                removeFQ(title);
            }
            else if(title->operation & OPERATION_INSTALL)
            {
                install(title->entry == NULL ? prettyDir(title->folderName) : title->entry->name, false, title->dlDev, title->folderName, title->toUSB, title->keepFiles, title->tmd, queueItemDone, self);
                removeFQ(title);
            }
            break;
        default: break;
    }
}

void processQueue(ResultCallback callback, void *userdata)
{
    // Pre-checks for space
    uint64_t sizes[3] = { 0, 0, 0 };
    TitleData *title;
    QUEUE_DATA qd = { .downloaded = 0, .dlSize = 0, .packages = 0, .current = 0, .eta = -1 };

    forEachListEntry(titleQueue, title)
    {
        if(title->operation & OPERATION_DOWNLOAD) qd.packages++;
        for(uint16_t i = 0; i < title->tmd->num_contents; ++i)
        {
            if(title->operation & OPERATION_INSTALL) sizes[title->toUSB ? 0 : 2] += title->tmd->contents[i].size;
            if(title->operation & OPERATION_DOWNLOAD)
            {
                qd.dlSize += title->tmd->contents[i].size;
                if(title->tmd->contents[i].type & TMD_CONTENT_TYPE_HASHED) qd.dlSize += getH3size(title->tmd->contents[i].size);
                if(title->keepFiles)
                {
                    int j = title->dlDev & NUSDEV_USB ? 0 : (title->dlDev & NUSDEV_SD ? 1 : 2);
                    if(title->tmd->contents[i].type & TMD_CONTENT_TYPE_HASHED) sizes[j] += getH3size(title->tmd->contents[i].size);
                    sizes[j] += title->tmd->contents[i].size;
                }
            }
        }
    }

    for(int i = 0; i < 3; ++i)
    {
        if(sizes[i] != 0)
        {
            NUSDEV toCheck = (i == 0) ? getUSB() : (i == 1 ? NUSDEV_SD : NUSDEV_MLC);
            if(!checkFreeSpace(toCheck, sizes[i])) { if(callback) callback(false, userdata); return; }
        }
    }

    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    if(self == NULL) return;
    ProcessQueueData *data = MEMAllocFromDefaultHeap(sizeof(ProcessQueueData));
    if(data == NULL) { MEMFreeToDefaultHeap(self); return; }

    data->queueData = qd;
    data->callback = callback;
    data->userdata = userdata;
    data->state = 0;

    self->onUpdate = processQueueUpdate;
    self->onDraw = NULL;
    self->onExit = NULL;
    self->data = data;
    self->dirty = false;

    screenPush(self);
}

bool removeFromQueue(uint32_t index)
{
    TitleData *title = getAndRemoveFromList(titleQueue, index);
    if(title == NULL)
        return false;

    if(title->rambuf != NULL)
        freeRamBuf(title->rambuf);
    else
        MEMFreeToDefaultHeap(title->tmd);

    MEMFreeToDefaultHeap(title);
    return true;
}

void clearQueue()
{
    TitleData *title;
    TitleData *last = NULL;
    forEachListEntry(titleQueue, title)
    {
        removeFQ(last);
        last = title;
    }

    removeFQ(last);
}

LIST *getTitleQueue()
{
    return titleQueue;
}
