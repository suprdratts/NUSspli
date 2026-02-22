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

#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include <config.h>
#include <file.h>
#include <input.h>
#include <localisation.h>
#include <menu/download.h>
#include <menu/predownload.h>
#include <menu/queue.h>
#include <menu/titlebrowser.h>
#include <menu/utils.h>
#include <queue.h>
#include <renderer.h>
#include <screen.h>
#include <state.h>
#include <titles.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#pragma GCC diagnostic pop

#define MAX_TITLEBROWSER_LINES (MAX_LINES - 5)

typedef struct
{
    TITLE_CATEGORY tab;
    size_t cursor;
    size_t pos;
    char search[129];
    size_t oldPos;
    TitleEntry **filteredTitleEntries;
    size_t filteredTitleEntrySize;
    uint32_t oldHold;
    size_t frameCount;
} TitleBrowserData;

static void drawTBMenuFrame(Screen *self)
{
    TitleBrowserData *data = (TitleBrowserData *)self->data;
    startNewFrame();

    const char *tabLabels[5] = { localise("Games"), localise("Updates"), localise("DLC"), localise("Demos"), localise("All") };
    for(uint32_t i = 0; i < 5; ++i)
        tabToFrame(0, i, tabLabels[i], i == data->tab);

    boxToFrame(1, MAX_LINES - 3);

    char *toFrame = getToFrameBuffer();
    strcpy(toFrame, localise("Press " BUTTON_A " to select"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_B " to return"));
    strcat(toFrame, " || ");
    strcat(toFrame, localise(BUTTON_X " to enter a title ID"));
    textToFrame(MAX_LINES - 2, ALIGNED_CENTER, toFrame);

    strcpy(toFrame, localise(BUTTON_Y " to search"));
    if(getListSize(getTitleQueue()))
    {
        strcat(toFrame, " || ");
        strcat(toFrame, localise(BUTTON_MINUS " to open the queue"));
    }
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, toFrame);

    if(data->pos != data->oldPos)
    {
        data->filteredTitleEntrySize = getTitleEntriesSize(data->tab);
        const TitleEntry *titleEntrys = getTitleEntries(data->tab);
        MCPRegion currentRegion = getRegion();
        size_t l = 0;

        if(data->search[0] != '\0')
        {
            char searchLower[129];
            strcpy(searchLower, data->search);
            for(size_t i = 0; searchLower[i]; ++i) searchLower[i] = tolower(searchLower[i]);

            char tmpName[MAX_TITLENAME_LENGTH];
            for(size_t i = 0; i < data->filteredTitleEntrySize; ++i)
            {
                if(!(currentRegion & titleEntrys[i].region)) continue;
                size_t max = strlen(titleEntrys[i].name);
                for(size_t j = 0; j < max; ++j) tmpName[j] = tolower(titleEntrys[i].name[j]);
                tmpName[max] = '\0';
                if(strstr(tmpName, searchLower))
                    data->filteredTitleEntries[l++] = (TitleEntry *)titleEntrys + i;
            }
        }
        else
            for(size_t i = 0; i < data->filteredTitleEntrySize; ++i)
                if(currentRegion & titleEntrys[i].region)
                    data->filteredTitleEntries[l++] = (TitleEntry *)titleEntrys + i;

        data->filteredTitleEntrySize = l;
        data->oldPos = data->pos;
    }

    size_t max = data->filteredTitleEntrySize - data->pos;
    if(max > MAX_TITLEBROWSER_LINES) max = MAX_TITLEBROWSER_LINES;
    MCPTitleListType titleList __attribute__((__aligned__(0x40)));
    TitleData *title;
    bool inQueue;
    for(size_t i = 0; i < max; ++i)
    {
        size_t l = i + 2;
        if(data->cursor == i) arrowToFrame(l, 1);
        size_t j = i + data->pos;
        if(MCP_GetTitleInfo(mcpHandle, data->filteredTitleEntries[j]->tid, &titleList) == 0) checkmarkToFrame(l, 4);
        flagToFrame(l, 7, data->filteredTitleEntries[j]->region);

        if(data->tab == TITLE_CATEGORY_ALL)
        {
            if(isDLC(data->filteredTitleEntries[j]->tid)) strcpy(toFrame, "[DLC] ");
            else if(isUpdate(data->filteredTitleEntries[j]->tid)) strcpy(toFrame, "[UPD] ");
            else toFrame[0] = '\0';
            strcat(toFrame, data->filteredTitleEntries[j]->name);
        }
        else strcpy(toFrame, data->filteredTitleEntries[j]->name);

        inQueue = false;
        forEachListEntry(getTitleQueue(), title) { if(title->entry == data->filteredTitleEntries[j]) { inQueue = true; break; } }
        if(inQueue) textToFrameColoredCut(l, 10, toFrame, SCREEN_COLOR_YELLOW, (SCREEN_WIDTH - (FONT_SIZE << 1)) - (getSpaceWidth() * 11));
        else textToFrameCut(l, 10, toFrame, (SCREEN_WIDTH - (FONT_SIZE << 1)) - (getSpaceWidth() * 11));
    }
    drawFrame();
}

static void titleBrowserExit(Screen *self)
{
    TitleBrowserData *data = (TitleBrowserData *)self->data;
    if(data)
    {
        if(data->filteredTitleEntries) MEMFreeToDefaultHeap(data->filteredTitleEntries);
        MEMFreeToDefaultHeap(data);
    }
    MEMFreeToDefaultHeap(self);
}

static void searchCallback(bool ok, const char *text, void *userdata)
{
    Screen *s = (Screen *)userdata;
    TitleBrowserData *d = (TitleBrowserData *)s->data;
    if(ok && text)
    {
        strncpy(d->search, text, 128);
        d->search[128] = '\0';
        d->cursor = d->pos = 0;
        d->oldPos = 99;
        s->dirty = true;
    }
}

static void titleBrowserUpdate(Screen *self)
{
    TitleBrowserData *data = (TitleBrowserData *)self->data;
    bool mov = data->filteredTitleEntrySize > MAX_TITLEBROWSER_LINES;
    bool dpadAction;

    if(vpad.trigger & VPAD_BUTTON_A)
    {
        const TitleEntry *entry = data->filteredTitleEntries[data->cursor + data->pos];
        predownloadMenu(entry);
        return;
    }

    if(vpad.trigger & VPAD_BUTTON_B)
    {
        screenPop();
        return;
    }

    if(vpad.hold & VPAD_BUTTON_UP)
    {
        if(data->oldHold != VPAD_BUTTON_UP) { data->oldHold = VPAD_BUTTON_UP; data->frameCount = 30; dpadAction = true; }
        else if(data->frameCount == 0) dpadAction = true;
        else { --data->frameCount; dpadAction = false; }
        if(dpadAction)
        {
            if(data->cursor) data->cursor--;
            else if(mov && data->pos) data->pos--;
            else if(!mov) data->cursor = data->filteredTitleEntrySize - 1;
            else { data->cursor = MAX_TITLEBROWSER_LINES - 1; data->pos = data->filteredTitleEntrySize - MAX_TITLEBROWSER_LINES; }
            data->oldPos = 99; self->dirty = true;
        }
    }
    else if(vpad.hold & VPAD_BUTTON_DOWN)
    {
        if(data->oldHold != VPAD_BUTTON_DOWN) { data->oldHold = VPAD_BUTTON_DOWN; data->frameCount = 30; dpadAction = true; }
        else if(data->frameCount == 0) dpadAction = true;
        else { --data->frameCount; dpadAction = false; }
        if(dpadAction)
        {
            if(data->cursor + data->pos >= data->filteredTitleEntrySize - 1) { data->cursor = data->pos = 0; }
            else if(data->cursor < MAX_TITLEBROWSER_LINES - 1) data->cursor++;
            else data->pos++;
            data->oldPos = 99; self->dirty = true;
        }
    }

    if(vpad.trigger & VPAD_BUTTON_X) { downloadMenu(); return; }
    if(vpad.trigger & VPAD_BUTTON_MINUS && getListSize(getTitleQueue())) { queueMenu(); self->dirty = true; }
    if(vpad.trigger & VPAD_BUTTON_Y) { showKeyboard(KEYBOARD_LAYOUT_NORMAL, KEYBOARD_TYPE_NORMAL, CHECK_NONE, 128, false, data->search, localise("Search"), searchCallback, self); }

    if(vpad.trigger & (VPAD_BUTTON_R | VPAD_BUTTON_ZR | VPAD_BUTTON_PLUS))
    {
        data->tab = (data->tab + 1) % 5;
        data->cursor = data->pos = 0; data->oldPos = 99; self->dirty = true;
    }
    else if(vpad.trigger & (VPAD_BUTTON_L | VPAD_BUTTON_ZL))
    {
        data->tab = (data->tab + 4) % 5;
        data->cursor = data->pos = 0; data->oldPos = 99; self->dirty = true;
    }

    if(data->oldHold && !(vpad.hold & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN))) data->oldHold = 0;
}

static void titleBrowserDraw(Screen *self) { drawTBMenuFrame(self); }

Screen *titleBrowserScreenGet()
{
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    TitleBrowserData *data = MEMAllocFromDefaultHeap(sizeof(TitleBrowserData));
    OSBlockSet(data, 0, sizeof(TitleBrowserData));
    data->tab = TITLE_CATEGORY_GAME; data->oldPos = 99;
    data->filteredTitleEntries = (TitleEntry **)MEMAllocFromDefaultHeap(getTitleEntriesSize(TITLE_CATEGORY_ALL) * sizeof(uintptr_t));
    self->onUpdate = titleBrowserUpdate; self->onDraw = titleBrowserDraw; self->onExit = titleBrowserExit; self->data = data; self->dirty = true;
    return self;
}

void titleBrowserMenu()
{
    Screen *s = titleBrowserScreenGet();
    if(s) screenPush(s);
}
