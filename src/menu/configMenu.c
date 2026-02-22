/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2023 V10lator <v10lator@myway.de>                    *
 * Copyright (c) 2022 Xpl0itU <DaThinkingChair@protonmail.com>             *
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

#include <config.h>
#include <input.h>
#include <menu/config.h>
#include <menu/download.h>
#include <menu/main.h>
#include <menu/utils.h>
#include <renderer.h>
#include <screen.h>
#include <state.h>
#include <swkbd_wrapper.h>
#include <titles.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#pragma GCC diagnostic pop

#define ENTRY_COUNT 4

typedef struct
{
    int cursorPos;
} ConfigMenuData;

static void drawConfigMenu(Screen *self)
{
    ConfigMenuData *data = (ConfigMenuData *)self->data;
    startNewFrame();
    char *toScreen = getToFrameBuffer();

    strcpy(toScreen, localise("Language:"));
    strcat(toScreen, " ");
    strcat(toScreen, localise(getLanguageString(getMenuLanguage())));
    textToFrame(0, 4, toScreen);

    strcpy(toScreen, localise("Online updates:"));
    strcat(toScreen, " ");
    strcat(toScreen, localise(updateCheckEnabled() ? "Enabled" : "Disabled"));
    textToFrame(1, 4, toScreen);

    strcpy(toScreen, localise("Auto resume failed downloads:"));
    strcat(toScreen, " ");
    strcat(toScreen, localise(autoResumeEnabled() ? "Enabled" : "Disabled"));
    textToFrame(2, 4, toScreen);

    strcpy(toScreen, localise("Notification method:"));
    strcat(toScreen, " ");
    strcat(toScreen, localise(getNotificationString(getNotificationMethod())));
    textToFrame(3, 4, toScreen);

    strcpy(toScreen, localise("Region:"));
    strcat(toScreen, " ");
    strcat(toScreen, localise(getFormattedRegion(getRegion())));
    textToFrame(4, 4, toScreen);

    lineToFrame(MAX_LINES - 2, SCREEN_COLOR_WHITE);
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, localise("Press " BUTTON_B " to return"));

    arrowToFrame(data->cursorPos, 0);

    drawFrame();
}

static inline void switchMenuLanguage()
{
    Swkbd_LanguageType lang = getMenuLanguage();

    if(vpad.trigger & VPAD_BUTTON_LEFT)
    {
        switch(lang)
        {
            case Swkbd_LanguageType__Invalid:
                lang = Swkbd_LanguageType__Welsh;
                break;
            case Swkbd_LanguageType__Welsh:
                lang = Swkbd_LanguageType__Turkish;
                break;
            case Swkbd_LanguageType__Turkish:
                lang = Swkbd_LanguageType__Italian;
                break;
            case Swkbd_LanguageType__Italian:
                lang = Swkbd_LanguageType__French;
                break;
            case Swkbd_LanguageType__French:
                lang = Swkbd_LanguageType__Portuguese_BR;
                break;
            case Swkbd_LanguageType__Portuguese_BR:
                lang = Swkbd_LanguageType__Portuguese;
                break;
            case Swkbd_LanguageType__Portuguese:
                lang = Swkbd_LanguageType__Spanish;
                break;
            case Swkbd_LanguageType__Spanish:
                lang = Swkbd_LanguageType__German;
                break;
            case Swkbd_LanguageType__German:
                lang = Swkbd_LanguageType__English;
                break;
            default:
                lang = Swkbd_LanguageType__Invalid;
                break;
        }
    }
    else
    {
        switch(lang)
        {
            case Swkbd_LanguageType__Invalid:
                lang = Swkbd_LanguageType__English;
                break;
            case Swkbd_LanguageType__English:
                lang = Swkbd_LanguageType__German;
                break;
            case Swkbd_LanguageType__German:
                lang = Swkbd_LanguageType__Spanish;
                break;
            case Swkbd_LanguageType__Spanish:
                lang = Swkbd_LanguageType__Portuguese;
                break;
            case Swkbd_LanguageType__Portuguese:
                lang = Swkbd_LanguageType__Portuguese_BR;
                break;
            case Swkbd_LanguageType__Portuguese_BR:
                lang = Swkbd_LanguageType__French;
                break;
            case Swkbd_LanguageType__French:
                lang = Swkbd_LanguageType__Italian;
                break;
            case Swkbd_LanguageType__Italian:
                lang = Swkbd_LanguageType__Turkish;
                break;
            case Swkbd_LanguageType__Turkish:
                lang = Swkbd_LanguageType__Welsh;
                break;
            default:
                lang = Swkbd_LanguageType__Invalid;
                break;
        }
    }

    setMenuLanguage(lang);
}

static inline void switchNotificationMethod()
{
    NOTIF_METHOD m = getNotificationMethod();

    if(vpad.trigger & VPAD_BUTTON_LEFT)
    {
        switch((int)m)
        {
            case NOTIF_METHOD_RUMBLE | NOTIF_METHOD_LED:
                m = NOTIF_METHOD_NONE;
                break;
            case NOTIF_METHOD_NONE:
                m = NOTIF_METHOD_RUMBLE;
                break;
            case NOTIF_METHOD_RUMBLE:
                m = NOTIF_METHOD_LED;
                break;
            default:
                m = NOTIF_METHOD_RUMBLE | NOTIF_METHOD_LED;
        }
    }
    else
    {
        switch((int)m)
        {
            case NOTIF_METHOD_RUMBLE | NOTIF_METHOD_LED:
                m = NOTIF_METHOD_LED;
                break;
            case NOTIF_METHOD_LED:
                m = NOTIF_METHOD_RUMBLE;
                break;
            case NOTIF_METHOD_RUMBLE:
                m = NOTIF_METHOD_NONE;
                break;
            default:
                m = NOTIF_METHOD_RUMBLE | NOTIF_METHOD_LED;
        }
    }

    setNotificationMethod(m);
}

static inline void switchRegion()
{
    MCPRegion reg = getRegion();

    if(vpad.trigger & VPAD_BUTTON_LEFT)
    {
        switch((int)reg)
        {
            case MCP_REGION_EUROPE | MCP_REGION_USA | MCP_REGION_JAPAN:
                reg = MCP_REGION_JAPAN;
                break;
            case MCP_REGION_JAPAN:
                reg = MCP_REGION_USA;
                break;
            case MCP_REGION_USA:
                reg = MCP_REGION_EUROPE;
                break;
            default:
                reg = MCP_REGION_EUROPE | MCP_REGION_USA | MCP_REGION_JAPAN;
        }
    }
    else
    {
        switch((int)reg)
        {
            case MCP_REGION_EUROPE | MCP_REGION_USA | MCP_REGION_JAPAN:
                reg = MCP_REGION_EUROPE;
                break;
            case MCP_REGION_EUROPE:
                reg = MCP_REGION_USA;
                break;
            case MCP_REGION_USA:
                reg = MCP_REGION_JAPAN;
                break;
            default:
                reg = MCP_REGION_EUROPE | MCP_REGION_USA | MCP_REGION_JAPAN;
        }
    }

    setRegion(reg);
}

static void configMenuUpdate(Screen *self)
{
    ConfigMenuData *data = (ConfigMenuData *)self->data;

    if(vpad.trigger & VPAD_BUTTON_B)
    {
        saveConfig(false);
        screenPop();
        return;
    }

    if(vpad.trigger & (VPAD_BUTTON_RIGHT | VPAD_BUTTON_LEFT | VPAD_BUTTON_A))
    {
        switch(data->cursorPos)
        {
            case 0:
                switchMenuLanguage();
                break;
            case 1:
                setUpdateCheck(!updateCheckEnabled());
                break;
            case 2:
                setAutoResume(!autoResumeEnabled());
                break;
            case 3:
                switchNotificationMethod();
                break;
            case 4:
                switchRegion();
                break;
        }

        self->dirty = true;
    }
    else if(vpad.trigger & VPAD_BUTTON_UP)
    {
        --data->cursorPos;
        if(data->cursorPos < 0)
            data->cursorPos = ENTRY_COUNT;
        self->dirty = true;
    }
    else if(vpad.trigger & VPAD_BUTTON_DOWN)
    {
        ++data->cursorPos;
        if(data->cursorPos > ENTRY_COUNT)
            data->cursorPos = 0;
        self->dirty = true;
    }
}

static void configMenuDraw(Screen *self)
{
    drawConfigMenu(self);
}

static void configMenuExit(Screen *self)
{
    if(self->data)
        MEMFreeToDefaultHeap(self->data);
    MEMFreeToDefaultHeap(self);
}

Screen *configMenuScreenGet()
{
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    if(self == NULL)
        return NULL;

    ConfigMenuData *data = MEMAllocFromDefaultHeap(sizeof(ConfigMenuData));
    if(data == NULL)
    {
        MEMFreeToDefaultHeap(self);
        return NULL;
    }

    data->cursorPos = 0;

    self->onUpdate = configMenuUpdate;
    self->onDraw = configMenuDraw;
    self->onExit = configMenuExit;
    self->data = data;
    self->dirty = true;

    return self;
}

void configMenu()
{
    Screen *s = configMenuScreenGet();
    if(s)
        screenPush(s);
}
