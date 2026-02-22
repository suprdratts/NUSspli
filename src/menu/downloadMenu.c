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

#include <stdint.h>
#include <string.h>

#include <input.h>
#include <menu/download.h>
#include <menu/predownload.h>
#include <menu/utils.h>
#include <renderer.h>
#include <state.h>
#include <staticMem.h>
#include <titles.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/filesystem_fsa.h>
#pragma GCC diagnostic pop

static void downloadKeyboardCallback(bool ok, const char *text, void *userdata)
{
    (void)userdata;
    if(ok && text != NULL)
    {
        char titleID[17];
        strncpy(titleID, text, 16);
        titleID[16] = '\0';
        toLowercase(titleID);
        uint64_t tid;
        hexToByte(titleID, (uint8_t *)&tid);

        const TitleEntry *entry = getTitleEntryByTid(tid);
        if(entry != NULL)
        {
            predownloadMenu(entry);
        }
    }
}

void downloadMenu()
{
    showKeyboard(KEYBOARD_LAYOUT_TID, KEYBOARD_TYPE_RESTRICTED, CHECK_HEXADECIMAL, 16, true, "00050000101", NULL, downloadKeyboardCallback, NULL);
}
