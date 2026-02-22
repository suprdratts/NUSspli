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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <ticket.h>

#include <crypto.h>
#include <downloader.h>
#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <ioQueue.h>
#include <keygen.h>
#include <list.h>
#include <localisation.h>
#include <menu/filebrowser.h>
#include <menu/utils.h>
#include <renderer.h>
#include <screen.h>
#include <state.h>
#include <titles.h>
#include <tmd.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

#define TICKET_BUCKET "/vol/slc/sys/rights/ticket/apps/"

typedef struct
{
    uint8_t *start;
    size_t size;
} TICKET_SECTION;

typedef struct WUT_PACKED
{
    uint32_t unk01;
    uint32_t unk02;
    uint32_t unk03;
    uint32_t unk04;
    uint16_t unk05;
    WUT_UNKNOWN_BYTES(0x06);
    uint32_t unk06[8];
    WUT_UNKNOWN_BYTES(0x60);
} TICKET_HEADER_SECTION;
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x04, unk02);
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x08, unk03);
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x0C, unk04);
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x10, unk05);
WUT_CHECK_OFFSET(TICKET_HEADER_SECTION, 0x18, unk06);
WUT_CHECK_SIZE(TICKET_HEADER_SECTION, 0x98);

static const uint8_t magic_header[10] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 };
static uint8_t default_cert[sizeof(OTHER_PPKI_CERT)] = { 0xff };

static void generateHeader(FileType type, NUS_HEADER *out)
{
    OSBlockMove(out->magic_header, magic_header, 10, false);
    OSBlockMove(out->app, "NUSspli", sizeof("NUSspli") - 1, false);
    OSBlockMove(out->app_version, NUSSPLI_VERSION, sizeof(NUSSPLI_VERSION) - 1, false);

    if(type == FILE_TYPE_TIK)
        OSBlockMove(out->file_type, "Ticket", sizeof("Ticket") - 1, false);
    else
        OSBlockMove(out->file_type, "Certificate", sizeof("Certificate") - 1, false);

    out->sig_type = type == FILE_TYPE_TIK ? 0x00010004 : 0x00010003;
    out->meta_version = 0x01;
    osslBytes(out->rand_area, sizeof(out->rand_area));
}

bool generateTik(const char *path, const TMD *tmd)
{
    TICKET ticket;
    OSBlockSet(&ticket, 0x00, sizeof(TICKET));

    if(!generateKey(tmd->tid, ticket.key))
        return false;

    generateHeader(FILE_TYPE_TIK, &ticket.header);
    osslBytes(&ticket.ecdsa_pubkey, sizeof(ticket.ecdsa_pubkey));
    osslBytes(&ticket.ticket_id, sizeof(uint64_t));
    ticket.ticket_id &= 0x0000FFFFFFFFFFFF;
    ticket.ticket_id |= 0x0005000000000000;

    OSBlockMove(ticket.issuer, "Root-CA00000003-XS0000000c", sizeof("Root-CA00000003-XS0000000c") - 1, false);

    ticket.version = 0x01;
    ticket.tid = tmd->tid;
    ticket.title_version = tmd->title_version;
    ticket.property_mask = 0xFFFF;

    ticket.header_version = 0x0001;
    if(!isDLC(tmd->tid))
        ticket.total_hdr_size = 0x00000014;
    else
    {
        ticket.total_hdr_size = 0x000000AC;
        ticket.sect_hdr_offset = 0x00000014;
        ticket.num_sect_headers = 0x0001;
        ticket.num_sect_header_entry_size = 0x0014;
    }

    FSAFileHandle tik = openFile(path, "w", 0);
    if(tik == 0)
    {
        char *err = getStaticScreenBuffer();
        sprintf(err, "%s\n%s", localise("Could not open path"), prettyDir(path));
        showErrorFrame(err);
        return false;
    }

    addToIOQueue(&ticket, 1, sizeof(TICKET), tik);

    if(isDLC(tmd->tid))
    {
        TICKET_HEADER_SECTION section;
        OSBlockSet(&section, 0x00, sizeof(TICKET_HEADER_SECTION));

        section.unk01 = 0x00000028;
        section.unk02 = 0x00000001;
        section.unk03 = 0x00000084;
        section.unk04 = 0x00000084;
        section.unk05 = 0x0003;
        for(int i = 0; i < 8; i++)
            section.unk06[i] = 0xFFFFFFFF;

        addToIOQueue(&section, 1, sizeof(TICKET_HEADER_SECTION), tik);
    }

    addToIOQueue(NULL, 0, 0, tik);
    return true;
}

static void *getCert(int id, const TMD *tmd)
{
    const uint8_t *ptr = (const uint8_t *)tmd;
    ptr += sizeof(TMD) + (sizeof(TMD_CONTENT) * tmd->num_contents);
    if(id == 0)
        ptr += sizeof(OTHER_PPKI_CERT);
    return (void *)ptr;
}

typedef struct
{
    const TMD *tmd;
    const TICKET *ticket;
    size_t ticketSize;
    char path[FS_MAX_PATH];
    ResultCallback callback;
    void *userdata;
} CertData;

static void downloadCertDone(bool result, void *userdata)
{
    CertData *data = (CertData *)userdata;
    if(result)
    {
        // Re-call generateCert, it should now have default_cert[0] != 0xff
        generateCert(data->tmd, data->ticket, data->ticketSize, data->path, data->callback, data->userdata);
    }
    else
    {
        if(data->callback) data->callback(false, data->userdata);
    }
    MEMFreeToDefaultHeap(data);
}

static void downloadCertDoneRAM(bool result, void *userdata)
{
    Screen *self = (Screen *)userdata;
    CertData *data = (CertData *)self->data;
    RAMBUF *rambuf = (RAMBUF *)data->userdata; // Hacky reuse

    if(result)
    {
        if(rambuf->size >= 0x350 + sizeof(OTHER_PPKI_CERT))
        {
            OSBlockMove(default_cert, rambuf->buf + 0x350, sizeof(OTHER_PPKI_CERT), false);
            generateCert(data->tmd, data->ticket, data->ticketSize, data->path, data->callback, data->userdata);
        }
        else result = false;
    }

    if(!result) { if(data->callback) data->callback(false, data->userdata); }
    freeRamBuf(rambuf);
    MEMFreeToDefaultHeap(data);
    screenPop();
}

void generateCert(const TMD *tmd, const TICKET *ticket, size_t ticketSize, const char *path, ResultCallback callback, void *userdata)
{
    CETK cetk;
    if(ticketSize == 0)
    {
        OSBlockSet(&cetk, 0x00, sizeof(CETK));
        OSBlockMove(cetk.cert1.issuer, "Root", sizeof("Root") - 1, false);
        OSBlockMove(cetk.cert1.type, "CA00000003", sizeof("CA00000003") - 1, false);
        OSBlockMove(cetk.cert2.issuer, "Root-CA00000003", sizeof("Root-CA00000003") - 1, false);
        OSBlockMove(cetk.cert2.type, "CP0000000b", sizeof("CP0000000b") - 1, false);
        OSBlockMove(cetk.cert3.issuer, "Root-CA00000003", sizeof("Root-CA00000003") - 1, false);
        OSBlockMove(cetk.cert3.type, "XS0000000c", sizeof("XS0000000c") - 1, false);
        osslBytes(&cetk.cert1.sig, sizeof(cetk.cert1.sig));
        osslBytes(&cetk.cert1.cert, sizeof(cetk.cert1.cert));
        osslBytes(&cetk.cert2.sig, sizeof(cetk.cert2.sig));
        osslBytes(&cetk.cert2.cert, sizeof(cetk.cert2.cert));
        osslBytes(&cetk.cert3.sig, sizeof(cetk.cert3.sig));
        cetk.cert1.sig_type = 0x00010003;
        cetk.cert1.version = 0x00000001;
        cetk.cert1.unknown_01 = 0x00010001;
        cetk.cert2.sig_type = 0x00010004;
        cetk.cert2.version = 0x00000001;
        cetk.cert2.unknown_01 = 0x00010001;
        cetk.cert3.sig_type = 0x00010004;
        cetk.cert3.version = 0x00000001;
        cetk.cert2.unknown_01 = 0x00010001;
        OSBlockSet(&cetk, 0x00, sizeof(NUS_HEADER));
        generateHeader(FILE_TYPE_CERT, (NUS_HEADER *)&cetk);
    }
    else
    {
        const uint8_t *ptr;
        if(ticketSize >= 0x350 + sizeof(OTHER_PPKI_CERT))
        {
            ptr = (uint8_t *)ticket;
            ptr += 0x350;
        }
        else
        {
            if(default_cert[0] == 0xff)
            {
                CertData *data = MEMAllocFromDefaultHeap(sizeof(CertData));
                if(data == NULL) { if(callback) callback(false, userdata); return; }
                data->tmd = tmd; data->ticket = ticket; data->ticketSize = ticketSize; strcpy(data->path, path); data->callback = callback; data->userdata = userdata;
                RAMBUF *rambuf = allocRamBuf();
                if(rambuf == NULL) { MEMFreeToDefaultHeap(data); if(callback) callback(false, userdata); return; }
                data->userdata = rambuf; // Abuse userdata to store rambuf
                downloadFile(DOWNLOAD_URL "000500101000400a/cetk", "OSv10 title.tik", NULL, FILE_TYPE_TIK | FILE_TYPE_TORAM, false, NULL, rambuf, downloadCertDoneRAM, data);
                return;
            }
            ptr = default_cert;
        }
        OSBlockMove(&cetk.cert1, getCert(0, tmd), sizeof(CA3_PPKI_CERT), false);
        OSBlockMove(&cetk.cert2, getCert(1, tmd), sizeof(OTHER_PPKI_CERT), false);
        OSBlockMove(&cetk.cert3, ptr, sizeof(OTHER_PPKI_CERT), false);
    }
    FSAFileHandle cert = openFile(path, "w", 0);
    if(cert == 0)
    {
        char *err = getStaticScreenBuffer();
        sprintf(err, "%s\n%s", localise("Could not open path"), prettyDir(path));
        showErrorFrame(err);
        if(callback) callback(false, userdata);
        return;
    }
    addToIOQueue(&cetk, 1, sizeof(CETK), cert);
    addToIOQueue(NULL, 0, 0, cert);
    if(callback) callback(true, userdata);
}

static void drawTicketFrame(uint64_t titleID)
{
    char tid[17];
    hex(titleID, 16, tid);
    startNewFrame();
    textToFrame(0, 0, localise("Title ID:"));
    textToFrame(1, 3, tid);
    int line = MAX_LINES - 1;
    textToFrame(line--, 0, localise("Press " BUTTON_B " to return"));
    textToFrame(line--, 0, localise("Press " BUTTON_A " to continue"));
    lineToFrame(line, SCREEN_COLOR_WHITE);
    drawFrame();
}

static void drawTicketGenFrame(const char *dir)
{
    colorStartNewFrame(SCREEN_COLOR_D_GREEN);
    textToFrame(0, 0, localise("Fake ticket generated on:"));
    textToFrame(1, 0, prettyDir(dir));
    textToFrame(3, 0, localise("Press any key to return"));
    drawFrame();
}

typedef struct
{
    char dir[FS_MAX_PATH];
    TMD *tmd;
} FakeTicketData;

static void fakeCertDone(bool result, void *userdata)
{
    Screen *self = (Screen *)userdata;
    FakeTicketData *data = (FakeTicketData *)self->data;
    if(result)
    {
        char tikPath[FS_MAX_PATH];
        strcpy(tikPath, data->dir);
        char *ptr = strrchr(tikPath, '.');
        if(ptr) strcpy(ptr + 1, "tik");
        if(generateTik(tikPath, data->tmd))
        {
            drawTicketGenFrame(tikPath);
            showErrorFrame(localise("Fake ticket generated successfully!"));
        }
    }
    screenPop();
}

static void fakeTicketUpdate(Screen *self)
{
    FakeTicketData *data = (FakeTicketData *)self->data;
    if(vpad.trigger & VPAD_BUTTON_A)
    {
        startNewFrame();
        textToFrame(0, 0, localise("Generating fake ticket..."));
        drawFrame();
        strcat(data->dir, "title.cert");
        generateCert(data->tmd, NULL, 0, data->dir, fakeCertDone, self);
        return;
    }
    if(vpad.trigger & VPAD_BUTTON_B)
    {
        screenPop();
        generateFakeTicket();
        return;
    }
}

static void fakeTicketDraw(Screen *self)
{
    FakeTicketData *data = (FakeTicketData *)self->data;
    drawTicketFrame(data->tmd->tid);
}

static void fakeTicketExit(Screen *self)
{
    FakeTicketData *data = (FakeTicketData *)self->data;
    if(data)
    {
        if(data->tmd) MEMFreeToDefaultHeap(data->tmd);
        MEMFreeToDefaultHeap(data);
    }
    MEMFreeToDefaultHeap(self);
}

static void ticketFileBrowserCallback(const char *path, void *userdata)
{
    (void)userdata;
    if(path == NULL) return;
    TMD *tmd = getTmd(path, false);
    if(tmd == NULL) { showErrorFrame(localise("Invalid title.tmd file!")); generateFakeTicket(); return; }
    Screen *self = MEMAllocFromDefaultHeap(sizeof(Screen));
    FakeTicketData *data = MEMAllocFromDefaultHeap(sizeof(FakeTicketData));
    OSBlockSet(data, 0, sizeof(FakeTicketData));
    strcpy(data->dir, path);
    data->tmd = tmd;
    self->onUpdate = fakeTicketUpdate;
    self->onDraw = fakeTicketDraw;
    self->onExit = fakeTicketExit;
    self->data = data;
    self->dirty = true;
    screenPush(self);
}

void generateFakeTicket()
{
    fileBrowserMenu(false, false, ticketFileBrowserCallback, NULL);
}

void deleteTicket(uint64_t tid)
{
    LIST *ticketList = createList();
    if(ticketList == NULL) return;
    char *path = getStaticPathBuffer(0);
    OSBlockMove(path, TICKET_BUCKET, sizeof(TICKET_BUCKET), false);
    char *inSentence = path + (sizeof(TICKET_BUCKET) - 1);
    FSADirectoryHandle dir;
    FSError ret = FSAOpenDir(getFSAClient(), path, &dir);
    if(ret != FS_ERROR_OK) { destroyList(ticketList, true); return; }
    FSADirectoryEntry entry;
    FSADirectoryHandle dir2;
    char *fileName;
    void *file;
    size_t fileSize;
    TICKET *ticket;
    TICKET_SECTION *sec;
    bool found;
    uint8_t *fileEnd;
    uint8_t *ptr;
    while(FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK)
    {
        if(!(entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 4) continue;
        strcpy(inSentence, entry.name);
        ret = FSAOpenDir(getFSAClient(), path, &dir2);
        if(ret == FS_ERROR_OK)
        {
            strcat(inSentence, "/");
            fileName = inSentence + strlen(inSentence);
            while(FSAReadDir(getFSAClient(), dir2, &entry) == FS_ERROR_OK)
            {
                if((entry.info.flags & FS_STAT_DIRECTORY) || strlen(entry.name) != 12) continue;
                strcpy(fileName, entry.name);
                fileSize = readFile(path, &file);
                if(file != NULL)
                {
                    ticket = (TICKET *)file;
                    fileEnd = ((uint8_t *)file) + fileSize;
                    found = false;
                    while(true)
                    {
                        ptr = ((uint8_t *)ticket) + sizeof(TICKET);
                        if(ticket->total_hdr_size > 0x14) ptr += ticket->total_hdr_size - 0x14;
                        if(ticket->tid == tid) found = true;
                        else
                        {
                            sec = MEMAllocFromDefaultHeap(sizeof(TICKET_SECTION));
                            if(sec) { sec->start = (uint8_t *)ticket; sec->size = ptr - sec->start; addToListEnd(ticketList, sec); }
                        }
                        if(ptr >= fileEnd) break;
                        ticket = (TICKET *)ptr;
                    }
                    if(found)
                    {
                        if(getListSize(ticketList) == 0) FSARemove(getFSAClient(), path);
                        else
                        {
                            FSAFileHandle fh = openFile(path, "w", 0);
                            forEachListEntry(ticketList, sec) addToIOQueue(sec->start, 1, sec->size, fh);
                            addToIOQueue(NULL, 0, 0, fh);
                        }
                    }
                    clearList(ticketList, true);
                    MEMFreeToDefaultHeap(file);
                }
            }
            FSACloseDir(getFSAClient(), dir2);
        }
    }
    FSACloseDir(getFSAClient(), dir);
    destroyList(ticketList, true);
}

bool hasMagicHeader(const TICKET *ticket)
{
    for(size_t i = 0; i < sizeof(magic_header); ++i)
        if(ticket->header.magic_header[i] != magic_header[i])
            return false;
    return ticket->header.meta_version == 0x01;
}
