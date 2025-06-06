/* mplog.c
 *
 * File format support for Micropross mplog files
 * Copyright (c) 2016 by Martin Kaiser <martin@kaiser.cx>
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


/*
   The mplog file format logs the communication between a contactless
   smartcard and a card reader. Such files contain information about the
   physical layer as well as the bytes exchanged between devices.
   Some commercial logging and testing tools by the French company Micropross
   use this format.

   The information used for implementing this wiretap module were
   obtained from reverse-engineering. There is no publicly available
   documentation of the mplog file format.

   Mplog files start with the string "MPCSII". This string is part of
   the header which is in total 0x80 bytes long.

   Following the header, the file is a sequence of 8 byte-blocks.
        data       (one byte)
        block type (one byte)
        timestamp  (six bytes)

   The timestamp is a counter in little-endian format. The counter is in
   units of 10ns.
*/

#include "config.h"
#include "mplog.h"

#include <string.h>
#include <wtap-int.h>
#include <file_wrappers.h>

/* the block types */
#define TYPE_PCD_PICC_A  0x70
#define TYPE_PICC_PCD_A  0x71
#define TYPE_PCD_PICC_B  0x72
#define TYPE_PICC_PCD_B  0x73
#define TYPE_UNKNOWN     0xFF

#define KNOWN_TYPE(x) \
( \
  ((x) == TYPE_PCD_PICC_A) || \
  ((x) == TYPE_PICC_PCD_A) || \
  ((x) == TYPE_PCD_PICC_B) || \
  ((x) == TYPE_PICC_PCD_B) \
)

#define MPLOG_BLOCK_SIZE 8

/* ISO14443 pseudo-header, see https://www.kaiser.cx/pcap-iso14443.html */
#define ISO14443_PSEUDO_HDR_VER  0
#define ISO14443_PSEUDO_HDR_LEN  4
/*  the two transfer events are the types that include a trailing CRC
    the CRC is always present in mplog files */
#define ISO14443_PSEUDO_HDR_PICC_TO_PCD  0xFF
#define ISO14443_PSEUDO_HDR_PCD_TO_PICC  0xFE


#define ISO14443_MAX_PKT_LEN    4096

#define PKT_BUF_LEN   (ISO14443_PSEUDO_HDR_LEN + ISO14443_MAX_PKT_LEN)


static int mplog_file_type_subtype = -1;

void register_mplog(void);

/* read the next packet, starting at the current position of fh
   as we know very little about the file format, our approach is rather simple:
   - we read block-by-block until a known block-type is found
        - this block's type is the type of the next packet
        - this block's timestamp will become the packet's timestamp
        - the data byte will be our packet's first byte
   - we carry on reading blocks and add the data bytes
     of all blocks of "our" type
   - if a different well-known block type is found, this is the end of
     our packet, we go back one block so that this block can be picked
     up as the start of the next packet
   - if two blocks of our packet's block type are more than 200us apart,
     we treat this as a packet boundary as described above
   */
static bool mplog_read_packet(wtap *wth, FILE_T fh, wtap_rec *rec,
        int *err, char **err_info)
{
    uint8_t *p, *start_p;
    /* --- the last block of a known type --- */
    uint64_t last_ctr = 0;
    /* --- the current block --- */
    uint8_t block[MPLOG_BLOCK_SIZE]; /* the entire block */
    uint8_t data, type; /* its data and block type bytes */
    uint64_t ctr; /* its timestamp counter */
    /* --- the packet we're assembling --- */
    int pkt_bytes = 0;
    uint8_t pkt_type = TYPE_UNKNOWN;
    /* the timestamp of the packet's first block,
       this will become the packet's timestamp */
    uint64_t pkt_ctr = 0;


    ws_buffer_assure_space(&rec->data, PKT_BUF_LEN);
    p = ws_buffer_start_ptr(&rec->data);
    start_p = p;

    /* leave space for the iso14443 pseudo header
       we can't create it until we've seen the entire packet */
    p += ISO14443_PSEUDO_HDR_LEN;

    do {
        if (!wtap_read_bytes_or_eof(fh, block, sizeof(block), err, err_info)) {
            /* If we've already read some data, if this failed with an EOF,
               so that *err is 0, it's a short read. */
            if (pkt_bytes != 0) {
                if (*err == 0)
                    *err = WTAP_ERR_SHORT_READ;
            }
            break;
        }
        data = block[0];
        type = block[1];
        ctr = pletoh48(&block[2]);

        if (pkt_type == TYPE_UNKNOWN) {
            if (KNOWN_TYPE(type)) {
                pkt_type = type;
                pkt_ctr = ctr;
            }
        }

        if (type == pkt_type) {
            if (last_ctr != 0) {
                /* if the distance to the last byte of the
                   same type is larger than 200us, this is very likely the
                   first byte of a new packet -> go back one block and exit
                   ctr and last_ctr are in units of 10ns
                   at 106kbit/s, it takes approx 75us to send one byte */
                if (ctr - last_ctr > 200*100) {
                    file_seek(fh, -MPLOG_BLOCK_SIZE, SEEK_CUR, err);
                    break;
                }
            }

            *p++ = data;
            pkt_bytes++;
            last_ctr = ctr;
        }
        else if (KNOWN_TYPE(type)) {
            file_seek(fh, -MPLOG_BLOCK_SIZE, SEEK_CUR, err);
            break;
        }
    } while (pkt_bytes < ISO14443_MAX_PKT_LEN);

    if (pkt_type == TYPE_UNKNOWN)
        return false;

    start_p[0] = ISO14443_PSEUDO_HDR_VER;

    if (pkt_type==TYPE_PCD_PICC_A || pkt_type==TYPE_PCD_PICC_B)
        start_p[1] = ISO14443_PSEUDO_HDR_PCD_TO_PICC;
    else
        start_p[1] = ISO14443_PSEUDO_HDR_PICC_TO_PCD;

    start_p[2] = pkt_bytes >> 8;
    start_p[3] = pkt_bytes & 0xFF;

    wtap_setup_packet_rec(rec, wth->file_encap);
    rec->block = wtap_block_create(WTAP_BLOCK_PACKET);
    rec->presence_flags = WTAP_HAS_TS | WTAP_HAS_CAP_LEN;
    rec->ts.secs = (time_t)((pkt_ctr*10)/(1000*1000*1000));
    rec->ts.nsecs = (int)((pkt_ctr*10)%(1000*1000*1000));
    rec->rec_header.packet_header.caplen = ISO14443_PSEUDO_HDR_LEN + pkt_bytes;
    rec->rec_header.packet_header.len = rec->rec_header.packet_header.caplen;

    return true;
}


static bool
mplog_read(wtap *wth, wtap_rec *rec, int *err, char **err_info,
           int64_t *data_offset)
{
    *data_offset = file_tell(wth->fh);

    return mplog_read_packet(wth, wth->fh, rec, err, err_info);
}


static bool
mplog_seek_read(wtap *wth, int64_t seek_off, wtap_rec *rec,
                int *err, char **err_info)
{
    if (-1 == file_seek(wth->random_fh, seek_off, SEEK_SET, err))
        return false;

    if (!mplog_read_packet(wth, wth->random_fh, rec, err, err_info)) {
        /* Even if we got an immediate EOF, that's an error. */
        if (*err == 0)
            *err = WTAP_ERR_SHORT_READ;
        return false;
    }
    return true;
}


wtap_open_return_val mplog_open(wtap *wth, int *err, char **err_info)
{
    bool ok;
    uint8_t magic[6];

    ok = wtap_read_bytes(wth->fh, magic, 6, err, err_info);
    if (!ok) {
        if (*err != WTAP_ERR_SHORT_READ)
            return WTAP_OPEN_ERROR;
        return WTAP_OPEN_NOT_MINE;
    }
    if (memcmp(magic, "MPCSII", 6) != 0)
        return WTAP_OPEN_NOT_MINE;

    wth->file_encap = WTAP_ENCAP_ISO14443;
    wth->snapshot_length = 0;
    wth->file_tsprec = WTAP_TSPREC_NSEC;

    wth->priv = NULL;

    wth->subtype_read = mplog_read;
    wth->subtype_seek_read = mplog_seek_read;
    wth->file_type_subtype = mplog_file_type_subtype;

    /* skip the file header */
    if (-1 == file_seek(wth->fh, 0x80, SEEK_SET, err))
        return WTAP_OPEN_ERROR;

    *err = 0;

    /*
     * Add an IDB; we don't know how many interfaces were
     * involved, so we just say one interface, about which
     * we only know the link-layer type, snapshot length,
     * and time stamp resolution.
     */
    wtap_add_generated_idb(wth);

    return WTAP_OPEN_MINE;
}

static const struct supported_block_type mplog_blocks_supported[] = {
    /*
     * We support packet blocks, with no comments or other options.
     */
    { WTAP_BLOCK_PACKET, MULTIPLE_BLOCKS_SUPPORTED, NO_OPTIONS_SUPPORTED }
};

static const struct file_type_subtype_info mplog_info = {
    "Micropross mplog", "mplog", "mplog", NULL,
    false, BLOCKS_SUPPORTED(mplog_blocks_supported),
    NULL, NULL, NULL
};

void register_mplog(void)
{
    mplog_file_type_subtype = wtap_register_file_type_subtype(&mplog_info);

    /*
     * Register name for backwards compatibility with the
     * wtap_filetypes table in Lua.
     */
    wtap_register_backwards_compatibility_lua_name("MPLOG",
                                                   mplog_file_type_subtype);
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
