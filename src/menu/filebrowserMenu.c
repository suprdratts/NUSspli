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

#include <dirent.h>
#include <stdbool.h>
#include <string.h>

#include <crypto.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <list.h>
#include <localisation.h>
#include <menu/filebrowser.h>
#include <menu/queue.h>
#include <menu/utils.h>
#include <queue.h>
#include <renderer.h>
#include <screen.h>
#include <state.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

#define MAX_FILEBROWSER_LINES (MAX_LINES - 5)

static NUSDEV activeDevice = NUSDEV_NONE;

typedef struct
{
    char *path;
    LIST *folders;
    size_t cursor;
    size_t pos;
    bool mov;
    bool installMenu;
    bool allowNoIntro;
    FileBrowserCallback callback;
    void *userdata;
    uint32_t oldHold;
    size_t frameCount;
    NUSDEV usbMounted;
} FileBrowserData;

static void drawFBMenuFrame(const char *path, LIST *folders, size_t pos, const size_t cursor, bool usbMounted, bool installMenu, bool showQueue)
{
    startNewFrame();
    textToFrame(0, 6, localise("Select a folder:"));

    boxToFrame(1, MAX_LINES - 3);

    char *toWrite = getToFrameBuffer();
    strcpy(toWrite, localise("Press " BUTTON_A " to select"));
    strcat(toWrite, " || ");
    strcat(toWrite, localise(BUTTON_B " to return"));
    strcat(toWrite, " || ");

    char l_str[128];
    strcpy(l_str, BUTTON_X " to switch to ");
    strcat(l_str, activeDevice == NUSDEV_USB ? "SD" : activeDevice == NUSDEV_SD ? "NAND"
            : usbMounted                                                        ? "USB"
                                                                                : "SD");
    strcat(toWrite, localise(l_str));
    textToFrame(MAX_LINES - 2, ALIGNED_CENTER, toWrite);

    if(showQueue)
    {
        strcpy(toWrite, localise(BUTTON_MINUS " to open the queue"));
        strcat(toWrite, " || ");
        strcat(toWrite, localise("Searching on"));
    }
    else
        strcpy(toWrite, localise("Searching on"));

    strcat(toWrite, " => ");
    strcat(toWrite, prettyDir(path));
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, toWrite);

    char *folder;
    TitleData *title;
    char fp[FS_MAX_PATH];
    size_t i = strlen(path);
    OSBlockMove(fp, path, i, false);
    char *l_ptr = fp + i;
    i = 0;
    bool sQ = false;

    forEachListEntry(folders, folder)
    {
        if(pos)
        {
            --pos;
            continue;
        }

        if(cursor == i)
            arrowToFrame(i + 2, 1);

        if(installMenu)
        {
            sQ = false;
            strcpy(l_ptr, folder);

            LIST *q = getTitleQueue();
            for(ELEMENT *q_cur = q->first; q_cur != NULL; q_cur = q_cur->next)
            {
                title = q_cur->content;
                if(strcmp(fp, title->folderName) == 0)
                {
                    sQ = true;
                    break;
                }
            }
        }

        if(sQ)
            textToFrameColored(i + 2, 5, folder, SCREEN_COLOR_YELLOW);
        else
            textToFrame(i + 2, 5, folder);

        if(++i == MAX_FILEBROWSER_LINES)
            break;
    }

    drawFrame();
}

static void refreshDirList(FileBrowserData *data)
{
    clearList(data->folders, true);
    char *name = MEMAllocFromDefaultHeap(sizeof("../"));
    if(name == NULL)
        return;

    OSBlockMove(name, "../", sizeof("../"), false);
    addToListEnd(data->folders, name);

    data->cursor = data->pos = 0;

    FSADirectoryHandle dir;
    if(FSAOpenDir(getFSAClient(), data->path, &dir) == FS_ERROR_OK)
    {
        FSADirectoryEntry entry;
        while(FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
            if(entry.info.flags & FS_STAT_DIRECTORY)
            {
                size_t len = strlen(entry.name);
                name = MEMAllocFromDefaultHeap(len + 2);
                if(name)
                {
                    OSBlockMove(name, entry.name, len, false);
                    name[len] = '/';
                    name[len + 1] = '\0';
                    addToListEnd(data->folders, name);
                }
            }
        FSACloseDir(getFSAClient(), dir);
    }
    data->mov = getListSize(data->folders) >= MAX_FILEBROWSER_LINES;
}

static void fileBrowserUpdate(Screen *self)
{
    FileBrowserData *data = (FileBrowserData *)self->data;
    bool sQ = data->installMenu ? getListSize(getTitleQueue()) : false;
    bool dpadAction;

    if(vpad.trigger & VPAD_BUTTON_B)
    {
        FileBrowserCallback cb = data->callback;
        void *ud = data->userdata;
        screenPop();
        if(cb)
            cb(NULL, ud);
        return;
    }

    if(vpad.trigger & VPAD_BUTTON_A)
    {
        if(data->cursor + data->pos == 0)
        {
            char *last = strstr(data->path + (sizeof("/vol/") - 1), "/");
            char *cur = strstr(last + 1, "/");
            if(cur != NULL)
            {
                char *next = strstr(cur + 1, "/");
                while(next != NULL)
                {
                    last = cur;
                    cur = next;
                    next = strstr(cur + 1, "/");
                }
                *++last = '\0';
                refreshDirList(data);
                self->dirty = true;
            }
        }
        else
        {
            char *sel = getContent(data->folders, data->cursor + data->pos);
            strcat(data->path, sel);
            size_t p = strlen(data->path);
            strcpy(data->path + p, "title.tmd");
            bool exists = fileExists(data->path);
            data->path[p] = '\0';
            if(exists)
            {
                FileBrowserCallback cb = data->callback;
                void *ud = data->userdata;
                char *path = data->path;
                data->path = NULL; // Prevent freeing in onExit
                screenPop();
                if(cb)
                    cb(path, ud);
                MEMFreeToDefaultHeap(path);
                return;
            }
            else if(data->allowNoIntro)
            {
                strcpy(data->path + p, "tmd");
                exists = fileExists(data->path);
                data->path[p] = '\0';
                if(exists)
                {
                    FileBrowserCallback cb = data->callback;
                    void *ud = data->userdata;
                    char *path = data->path;
                    data->path = NULL;
                    screenPop();
                    if(cb)
                        cb(path, ud);
                    MEMFreeToDefaultHeap(path);
                    return;
                }
            }
            refreshDirList(data);
            self->dirty = true;
        }
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
                data->cursor--;
            else if(data->mov && data->pos)
                data->pos--;
            else if(!data->mov)
                data->cursor = getListSize(data->folders) - 1;
            else
            {
                data->cursor = MAX_FILEBROWSER_LINES - 1;
                data->pos = getListSize(data->folders) - MAX_FILEBROWSER_LINES;
            }
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
            if(data->cursor + data->pos >= getListSize(data->folders) - 1)
            {
                data->cursor = data->pos = 0;
            }
            else if(data->cursor < MAX_FILEBROWSER_LINES - 1)
                data->cursor++;
            else
                data->pos++;
            self->dirty = true;
        }
    }

    if(vpad.trigger & VPAD_BUTTON_X)
    {
        switch((int)activeDevice)
        {
            case NUSDEV_USB:
                activeDevice = NUSDEV_SD;
                break;
            case NUSDEV_SD:
                activeDevice = NUSDEV_MLC;
                break;
            case NUSDEV_MLC:
                activeDevice = data->usbMounted ? NUSDEV_USB : NUSDEV_SD;
        }
        strcpy(data->path, (activeDevice & NUSDEV_USB) ? (data->usbMounted == NUSDEV_USB01 ? INSTALL_DIR_USB1 : INSTALL_DIR_USB2) : (activeDevice == NUSDEV_SD ? INSTALL_DIR_SD : INSTALL_DIR_MLC));
        refreshDirList(data);
        self->dirty = true;
    }

    if(vpad.trigger & VPAD_BUTTON_MINUS && sQ)
    {
        queueMenu();
    }

    if(data->oldHold && !(vpad.hold & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN)))
        data->oldHold = 0;
}

static void fileBrowserDraw(Screen *self)
{
    FileBrowserData *data = (FileBrowserData *)self->data;
    bool sQ = data->installMenu ? getListSize(getTitleQueue()) : false;
    drawFBMenuFrame(data->path, data->folders, data->pos, data->cursor, data->usbMounted != NUSDEV_NONE, data->installMenu, sQ);
}

static void fileBrowserExit(Screen *self)
{
    FileBrowserData *data = (FileBrowserData *)self->data;
    if(data)
    {
        if(data->folders)
            destroyList(data->folders, true);
        if(data->path)
            MEMFreeToDefaultHeap(data->path);
        MEMFreeToDefaultHeap(data);
    }
    MEMFreeToDefaultHeap(self);
}

Screen *fileBrowserScreenGet(bool installMenu, bool allowNoIntro, FileBrowserCallback callback, void *userdata)
{
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    if(self == NULL)
        return NULL;
    FileBrowserData *data = MEMAllocFromDefaultHeap(sizeof(FileBrowserData));
    if(data == NULL)
    {
        MEMFreeToDefaultHeap(self);
        return NULL;
    }
    OSBlockSet(data, 0, sizeof(FileBrowserData));
    data->path = MEMAllocFromDefaultHeap(FS_MAX_PATH);
    data->folders = createList();
    data->installMenu = installMenu;
    data->allowNoIntro = allowNoIntro;
    data->callback = callback;
    data->userdata = userdata;
    data->usbMounted = getUSB();
    if(activeDevice == NUSDEV_NONE)
        activeDevice = data->usbMounted ? NUSDEV_USB : NUSDEV_SD;
    strcpy(data->path, (activeDevice & NUSDEV_USB) ? (data->usbMounted == NUSDEV_USB01 ? INSTALL_DIR_USB1 : INSTALL_DIR_USB2) : (activeDevice == NUSDEV_SD ? INSTALL_DIR_SD : INSTALL_DIR_MLC));
    refreshDirList(data);
    self->onUpdate = fileBrowserUpdate;
    self->onDraw = fileBrowserDraw;
    self->onExit = fileBrowserExit;
    self->data = data;
    self->dirty = true;
    return self;
}

void fileBrowserMenu(bool installMenu, bool allowNoIntro, FileBrowserCallback callback, void *userdata)
{
    Screen *s = fileBrowserScreenGet(installMenu, allowNoIntro, callback, userdata);
    if(s)
        screenPush(s);
}
