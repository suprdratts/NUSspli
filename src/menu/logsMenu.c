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

#include <input.h>
#include <menu/logs.h>
#include <menu/utils.h>
#include <renderer.h>
#include <screen.h>
#include <state.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#pragma GCC diagnostic pop

static void drawLogsFrame()
{
    startNewFrame();
    writeScreenLog(-1);
    drawFrame();
}

static void logsMenuUpdate(Screen *self)
{
    (void)self;
    if(vpad.trigger)
    {
        screenPop();
        return;
    }
}

static void logsMenuDraw(Screen *self)
{
    (void)self;
    drawLogsFrame();
}

static void logsMenuExit(Screen *self)
{
    MEMFreeToDefaultHeap(self);
}

Screen *logsMenuScreenGet()
{
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    if(self == NULL)
        return NULL;

    self->onUpdate = logsMenuUpdate;
    self->onDraw = logsMenuDraw;
    self->onExit = logsMenuExit;
    self->data = NULL;
    self->dirty = true;

    return self;
}

void logsMenu()
{
    Screen *s = logsMenuScreenGet();
    if(s)
        screenPush(s);
}
