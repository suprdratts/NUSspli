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

#include <list.h>
#include <renderer.h>
#include <screen.h>
#include <state.h>
#include <stddef.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#pragma GCC diagnostic pop

static LIST *screenStack = NULL;

void screenInit()
{
    if(screenStack == NULL)
        screenStack = createList();
}

void screenExit()
{
    if(screenStack != NULL)
    {
        Screen *screen;
        while(getListSize(screenStack) > 0)
        {
            screen = (Screen *)getAndRemoveFromList(screenStack, 0);
            if(screen->onExit)
                screen->onExit(screen);
        }
        destroyList(screenStack, false);
        screenStack = NULL;
    }
}

void screenPush(Screen *screen)
{
    if(screenStack == NULL)
        screenInit();

    addToListBeginning(screenStack, screen);
    screen->dirty = true;
}

void screenPop()
{
    if(screenStack != NULL && getListSize(screenStack) > 0)
    {
        Screen *screen = (Screen *)getAndRemoveFromList(screenStack, 0);
        if(screen->onExit)
            screen->onExit(screen);

        Screen *top = screenGetTop();
        if(top)
            top->dirty = true;
    }
}

Screen *screenGetTop()
{
    if(screenStack != NULL && getListSize(screenStack) > 0)
        return (Screen *)getContent(screenStack, 0);

    return NULL;
}

void screenMainLoop()
{
    while(AppRunning(true))
    {
        if(app == APP_STATE_BACKGROUND)
            continue;

        Screen *top = screenGetTop();
        if(top == NULL)
            break;

        if(app == APP_STATE_RETURNING)
        {
            top->dirty = true;
            app = APP_STATE_RUNNING;
        }

        if(top->dirty)
        {
            top->onDraw(top);
            top->dirty = false;
        }

        showFrame();
        top->onUpdate(top);
    }
}
