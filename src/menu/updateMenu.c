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

#include <stdio.h>
#include <string.h>

#include <input.h>
#include <menu/update.h>
#include <menu/utils.h>
#include <renderer.h>
#include <screen.h>
#include <state.h>
#include <updater.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#pragma GCC diagnostic pop

typedef struct
{
    char newVersion[32];
    NUSSPLI_TYPE type;
} UpdateMenuData;

static void updateMenuUpdate(Screen *self)
{
    UpdateMenuData *data = (UpdateMenuData *)self->data;

    if(vpad.trigger & VPAD_BUTTON_A)
    {
        char version[32];
        strcpy(version, data->newVersion);
        NUSSPLI_TYPE type = data->type;
        screenPop();
        update(version, type, NULL, NULL);
    }
    else if(vpad.trigger & VPAD_BUTTON_B)
    {
        screenPop();
    }
}

static void updateMenuDraw(Screen *self)
{
    UpdateMenuData *data = (UpdateMenuData *)self->data;
    startNewFrame();

    char *toFrame = getToFrameBuffer();
    sprintf(toFrame, "%s: v%s", localise("New version available"), data->newVersion);
    textToFrame(0, ALIGNED_CENTER, toFrame);

    textToFrame(2, ALIGNED_CENTER, localise("Do you want to update now?"));

    textToFrame(MAX_LINES - 2, ALIGNED_CENTER, localise(BUTTON_A " to update now"));
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, localise(BUTTON_B " to return to the menu"));

    drawFrame();
}

static void updateMenuExit(Screen *self)
{
    if(self->data)
        MEMFreeToDefaultHeap(self->data);
    MEMFreeToDefaultHeap(self);
}

void updateMenu(const char *newVersion, NUSSPLI_TYPE type)
{
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    if(self == NULL)
        return;

    UpdateMenuData *data = MEMAllocFromDefaultHeap(sizeof(UpdateMenuData));
    if(data == NULL)
    {
        MEMFreeToDefaultHeap(self);
        return;
    }

    strncpy(data->newVersion, newVersion, 31);
    data->type = type;

    self->onUpdate = updateMenuUpdate;
    self->onDraw = updateMenuDraw;
    self->onExit = updateMenuExit;
    self->data = data;
    self->dirty = true;

    screenPush(self);
}
