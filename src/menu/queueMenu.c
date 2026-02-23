/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2022 Xpl0itU <DaThinkingChair@protonmail.com>             *
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

#include <input.h>
#include <list.h>
#include <localisation.h>
#include <menu/main.h>
#include <menu/queue.h>
#include <menu/utils.h>
#include <queue.h>
#include <renderer.h>
#include <screen.h>
#include <state.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

#define MAX_ENTRIES (MAX_LINES - 4)
#define SPACER      7
#define SPACER_END  14

typedef struct
{
    uint32_t oldHold;
    size_t frameCount;
    size_t cursor;
    size_t pos;
} QueueData;

static void drawQueueMenuFrame(LIST *titleQueue, size_t cursor, size_t pos)
{
    startNewFrame();
    boxToFrame(0, MAX_LINES - 3);

    char *toScreen = getToFrameBuffer();
    size_t i = 0;
    int p;
    TitleData *data;
    MCPRegion region;

    forEachListEntry(titleQueue, data)
    {
        if(pos)
        {
            --pos;
            continue;
        }

        if(cursor == i++)
            arrowToFrame(i, 1);

        if(data->operation & OPERATION_DOWNLOAD)
        {
            switch(data->dlDev)
            {
                case NUSDEV_SD:
                    deviceToFrame(i, 4, DEVICE_TYPE_SD);
                    break;
                case NUSDEV_MLC:
                    deviceToFrame(i, 4, DEVICE_TYPE_NAND);
                    break;
                default:
                    deviceToFrame(i, 4, DEVICE_TYPE_USB);
                    break;
            }
        }

        if(data->operation & OPERATION_INSTALL)
            deviceToFrame(i, SPACER, data->toUSB ? DEVICE_TYPE_USB : DEVICE_TYPE_NAND);

        if(isDLC(data->tmd->tid))
        {
            p = sizeof("[DLC] ") - 1;
            OSBlockMove(toScreen, "[DLC] ", p, false);
        }
        else if(isUpdate(data->tmd->tid))
        {
            p = sizeof("[UPD] ") - 1;
            OSBlockMove(toScreen, "[UPD] ", p, false);
        }
        else
            p = 0;

        if(data->entry == NULL)
        {
            region = MCP_REGION_UNKNOWN;
            strcpy(toScreen + p, prettyDir(data->folderName));
        }
        else
        {
            region = data->entry->region;
            strcpy(toScreen + p, data->entry->name);
        }

        flagToFrame(i, SPACER + 3, region);
        textToFrameCut(i, SPACER + 6, toScreen, (SCREEN_WIDTH - (FONT_SIZE << 1)) - (getSpaceWidth() * SPACER_END));

        if(i == MAX_ENTRIES)
            break;
    }

    textToFrame(MAX_LINES - 2, ALIGNED_CENTER, localise("Press " BUTTON_B " to return"));

    strcpy(toScreen, localise(BUTTON_PLUS " to start the queue"));
    strcat(toScreen, " || ");
    strcat(toScreen, localise(BUTTON_MINUS " to delete an item"));
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, toScreen);

    drawFrame();
}

static void queueProcessedCallback(bool result, void *userdata)
{
    (void)userdata;
    if(result)
    {
        showFinishedScreen(NULL, FINISHING_OPERATION_QUEUE);
    }
}

static void queueUpdate(Screen *self)
{
    QueueData *data = (QueueData *)self->data;
    LIST *titleQueue = getTitleQueue();
    bool mov = getListSize(titleQueue) >= MAX_ENTRIES;
    bool dpadAction;

    if(vpad.trigger & VPAD_BUTTON_B)
    {
        screenPop();
        return;
    }

    if(vpad.hold & VPAD_BUTTON_UP)
    {
        if(data->oldHold != VPAD_BUTTON_UP)
        {
            data->oldHold = VPAD_BUTTON_UP;
            data->frameCount = 30;
            dpadAction = true;
        }
        else if(data->frameCount == 0)
            dpadAction = true;
        else
        {
            --data->frameCount;
            dpadAction = false;
        }

        if(dpadAction)
        {
            if(data->cursor)
                --data->cursor;
            else if(data->pos)
                --data->pos;

            self->dirty = true;
        }
    }
    else if(vpad.hold & VPAD_BUTTON_DOWN)
    {
        if(data->oldHold != VPAD_BUTTON_DOWN)
        {
            data->oldHold = VPAD_BUTTON_DOWN;
            data->frameCount = 30;
            dpadAction = true;
        }
        else if(data->frameCount == 0)
            dpadAction = true;
        else
        {
            --data->frameCount;
            dpadAction = false;
        }

        if(dpadAction)
        {
            if(data->cursor < getListSize(titleQueue) - data->pos - 1 && data->cursor < MAX_ENTRIES - 1)
                ++data->cursor;
            else if(mov && data->cursor + ++data->pos == getListSize(titleQueue))
                --data->pos;

            self->dirty = true;
        }
    }
    else if(mov)
    {
        if(vpad.hold & VPAD_BUTTON_RIGHT)
        {
            if(data->oldHold != VPAD_BUTTON_RIGHT)
            {
                data->oldHold = VPAD_BUTTON_RIGHT;
                data->frameCount = 30;
                dpadAction = true;
            }
            else if(data->frameCount == 0)
                dpadAction = true;
            else
            {
                --data->frameCount;
                dpadAction = false;
            }

            if(dpadAction)
            {
                data->pos += MAX_ENTRIES;
                if(data->pos >= getListSize(titleQueue))
                    data->pos = 0;

                data->cursor = 0;
                self->dirty = true;
            }
        }
        else if(vpad.hold & VPAD_BUTTON_LEFT)
        {
            if(data->oldHold != VPAD_BUTTON_LEFT)
            {
                data->oldHold = VPAD_BUTTON_LEFT;
                data->frameCount = 30;
                dpadAction = true;
            }
            else if(data->frameCount == 0)
                dpadAction = true;
            else
            {
                --data->frameCount;
                dpadAction = false;
            }

            if(dpadAction)
            {
                if(data->pos >= MAX_ENTRIES)
                    data->pos -= MAX_ENTRIES;
                else
                    data->pos = getListSize(titleQueue) - MAX_ENTRIES;
                data->cursor = 0;
                self->dirty = true;
            }
        }
    }

    if(vpad.trigger & VPAD_BUTTON_PLUS)
    {
        screenPop();
        processQueue(queueProcessedCallback, NULL);
        return;
    }

    if(vpad.trigger & VPAD_BUTTON_MINUS)
    {
        removeFromQueue(data->cursor + data->pos);
        if(getListSize(titleQueue) == 0)
        {
            screenPop();
            return;
        }

        if(data->cursor + data->pos == getListSize(titleQueue))
        {
            if(data->cursor)
                --data->cursor;
            else if(data->pos)
                --data->pos;
        }

        self->dirty = true;
    }

    if(data->oldHold && !(vpad.hold & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT)))
        data->oldHold = 0;
}

static void queueDraw(Screen *self)
{
    QueueData *data = (QueueData *)self->data;
    drawQueueMenuFrame(getTitleQueue(), data->cursor, data->pos);
}

static void queueExit(Screen *self)
{
    if(self->data)
        MEMFreeToDefaultHeap(self->data);
    MEMFreeToDefaultHeap(self);
}

Screen *queueScreenGet()
{
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    if(self == NULL)
        return NULL;

    QueueData *data = MEMAllocFromDefaultHeap(sizeof(QueueData));
    if(data == NULL)
    {
        MEMFreeToDefaultHeap(self);
        return NULL;
    }

    OSBlockSet(data, 0, sizeof(QueueData));

    self->onUpdate = queueUpdate;
    self->onDraw = queueDraw;
    self->onExit = queueExit;
    self->data = data;
    self->dirty = true;

    return self;
}

void queueMenu()
{
    Screen *s = queueScreenGet();
    if(s)
        screenPush(s);
}
