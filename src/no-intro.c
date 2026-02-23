/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2023 V10lator <v10lator@myway.de>                         *
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
#include <string.h>

#include <file.h>
#include <filesystem.h>
#include <ioQueue.h>
#include <no-intro.h>
#include <screen.h>
#include <ticket.h>
#include <tmd.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

void destroyNoIntroData(NO_INTRO_DATA *data)
{
    if(data->path)
        MEMFreeToDefaultHeap(data->path);
    MEMFreeToDefaultHeap(data);
}

void revertNoIntro(NO_INTRO_DATA *data)
{
    char *newPath = MEMAllocFromDefaultHeap(FS_MAX_PATH);
    if(newPath == NULL)
    {
        debugPrintf("EOM!");
        return;
    }

    size_t s = strlen(data->path);
    OSBlockMove(newPath, data->path, s + 1, false);
    char *dataP = data->path + s;
    char *toP = newPath + s;

    OSBlockMove(dataP, "title.tik", sizeof("title.tik"), false);
    FSError ret;

    flushIOQueue();
    if(!data->hadTicket)
    {
        ret = FSARemove(getFSAClient(), data->path);
        if(ret != FS_ERROR_OK)
            debugPrintf("Can't remove %s: %s", data->path, translateFSErr(ret));
    }
    else
    {
        OSBlockMove(toP, "cetk", sizeof("cetk"), false);
        ret = FSARename(getFSAClient(), data->path, newPath);
        if(ret != FS_ERROR_OK)
            debugPrintf("Can't move %s to %s: %s", data->path, newPath, translateFSErr(ret));
    }

    OSBlockMove(dataP, "title.cert", sizeof("title.cert"), false);
    ret = FSARemove(getFSAClient(), data->path);
    if(ret != FS_ERROR_OK)
        debugPrintf("Can't remove %s: %s", data->path, translateFSErr(ret));

    OSBlockMove(dataP, "title.tmd", sizeof("title.tmd"), false);
    OSBlockMove(toP, "tmd", sizeof("tmd"), false);
    ret = FSARename(getFSAClient(), data->path, newPath);
    if(ret != FS_ERROR_OK)
        debugPrintf("Can't move %s to %s: %s", data->path, newPath, translateFSErr(ret));

    *dataP = '\0';
    FSADirectoryHandle dir;
    ret = FSAOpenDir(getFSAClient(), data->path, &dir);
    if(ret == FS_ERROR_OK)
    {
        FSADirectoryEntry entry;
        toP[8] = '\0';
        while(FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
        {
            if((entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 12 || strcmp(entry.name + 8, ".app") != 0)
                continue;

            OSBlockMove(dataP, entry.name, 13, false);
            OSBlockMove(toP, entry.name, 8, false);
            if(FSARename(getFSAClient(), data->path, newPath) != FS_ERROR_OK)
                debugPrintf("Can't move %s to %s: %s", data->path, newPath, translateFSErr(ret));
        }

        FSACloseDir(getFSAClient(), dir);
    }
    else
        debugPrintf("Can't open %s: %s", data->path, translateFSErr(ret));

    destroyNoIntroData(data);
    MEMFreeToDefaultHeap(newPath);
}

typedef struct
{
    NO_INTRO_DATA *data;
    char *pathTo;
    NoIntroCallback callback;
    void *userdata;
    int state;
    FSADirectoryHandle dir;
} TransformData;

static void transformDone(bool result, void *userdata)
{
    (void)result;
    Screen *self = (Screen *)userdata;
    TransformData *td = (TransformData *)self->data;
    td->state = 4; // Finished cert
}

static void transformUpdate(Screen *self)
{
    TransformData *td = (TransformData *)self->data;
    FSADirectoryEntry entry;
    FSError ret;
    char *fromP = td->data->path + strlen(td->data->path);
    char *toP = td->pathTo + strlen(td->data->path);

    switch(td->state)
    {
        case 0: // Initialize dir
            ret = FSAOpenDir(getFSAClient(), td->data->path, &td->dir);
            if(ret != FS_ERROR_OK)
            {
                NoIntroCallback cb = td->callback;
                void *ud = td->userdata;
                screenPop();
                if(cb)
                    cb(NULL, ud);
                return;
            }
            td->state = 1;
            break;
        case 1: // Iterate dir
            if(FSAReadDir(getFSAClient(), td->dir, &entry) == FS_ERROR_OK)
            {
                if(entry.info.flags & FS_STAT_DIRECTORY)
                    return;
                strcpy(fromP, entry.name);
                if(strcmp(entry.name, "tmd") == 0)
                {
                    strcpy(toP, "title.tmd");
                    if(FSARename(getFSAClient(), td->data->path, td->pathTo) == FS_ERROR_OK)
                        td->data->tmdFound = true;
                }
                else if(strcmp(entry.name, "cetk") == 0)
                {
                    strcpy(toP, "title.tik");
                    if(FSARename(getFSAClient(), td->data->path, td->pathTo) == FS_ERROR_OK)
                        td->data->hadTicket = true;
                }
                else if(strlen(entry.name) == 8)
                {
                    strcpy(toP, entry.name);
                    strcat(toP, ".app");
                    if(FSARename(getFSAClient(), td->data->path, td->pathTo) == FS_ERROR_OK)
                        td->data->ac++;
                }
            }
            else
            {
                FSACloseDir(getFSAClient(), td->dir);
                td->dir = 0;
                *fromP = '\0';
                if(!td->data->tmdFound || !td->data->ac)
                {
                    NO_INTRO_DATA *data = td->data;
                    td->data = NULL;
                    NoIntroCallback cb = td->callback;
                    void *ud = td->userdata;
                    revertNoIntro(data);
                    screenPop();
                    if(cb)
                        cb(NULL, ud);
                    return;
                }
                td->state = 2;
            }
            break;
        case 2: // TMD and TIK
        {
            TMD *tmd = getTmd(td->data->path, false);
            if(tmd == NULL)
            {
                NO_INTRO_DATA *data = td->data;
                td->data = NULL;
                NoIntroCallback cb = td->callback;
                void *ud = td->userdata;
                revertNoIntro(data);
                screenPop();
                if(cb)
                    cb(NULL, ud);
                return;
            }
            if(!td->data->hadTicket)
            {
                strcpy(fromP, "title.tik");
                if(!generateTik(td->data->path, tmd))
                {
                    MEMFreeToDefaultHeap(tmd);
                    NO_INTRO_DATA *data = td->data;
                    td->data = NULL;
                    NoIntroCallback cb = td->callback;
                    void *ud = td->userdata;
                    revertNoIntro(data);
                    screenPop();
                    if(cb)
                        cb(NULL, ud);
                    return;
                }
            }
            strcpy(fromP, "title.cert");
            td->state = 3; // Waiting for cert
            generateCert(tmd, NULL, 0, td->data->path, transformDone, self);
            MEMFreeToDefaultHeap(tmd);
            break;
        }
        case 4: // Finished cert
        {
            *fromP = '\0';
            NoIntroCallback cb = td->callback;
            NO_INTRO_DATA *data = td->data;
            void *ud = td->userdata;
            td->data = NULL; // Prevent freeing in onExit
            screenPop();
            if(cb)
                cb(data, ud);
            break;
        }
        default:
            break;
    }
}

static void transformExit(Screen *self)
{
    TransformData *td = (TransformData *)self->data;
    if(td)
    {
        if(td->dir)
            FSACloseDir(getFSAClient(), td->dir);
        if(td->pathTo)
            MEMFreeToDefaultHeap(td->pathTo);
        if(td->data)
            destroyNoIntroData(td->data);
        MEMFreeToDefaultHeap(td);
    }
    MEMFreeToDefaultHeap(self);
}

void transformNoIntro(const char *path, NoIntroCallback callback, void *userdata)
{
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    TransformData *td = MEMAllocFromDefaultHeap(sizeof(TransformData));
    if(!self || !td)
    {
        if(self)
            MEMFreeToDefaultHeap(self);
        if(td)
            MEMFreeToDefaultHeap(td);
        if(callback)
            callback(NULL, userdata);
        return;
    }

    OSBlockSet(td, 0, sizeof(TransformData));
    td->data = MEMAllocFromDefaultHeap(sizeof(NO_INTRO_DATA));
    td->data->path = MEMAllocFromDefaultHeap(FS_MAX_PATH);
    td->pathTo = MEMAllocFromDefaultHeap(FS_MAX_PATH);
    strcpy(td->data->path, path);
    if(td->data->path[strlen(path) - 1] != '/')
        strcat(td->data->path, "/");
    strcpy(td->pathTo, td->data->path);
    td->callback = callback;
    td->userdata = userdata;
    td->state = 0;

    self->onUpdate = transformUpdate;
    self->onDraw = NULL;
    self->onExit = transformExit;
    self->data = td;
    self->dirty = false;

    screenPush(self);
}
