/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#include "rendering/woff.h"

/* Prototypes only; the implementation is compiled in image_loader.c. */
#include "stb_image.h"

#include <stdlib.h>
#include <string.h>

#define WOFF_HEADER_SIZE      44
#define WOFF_DIR_ENTRY_SIZE   20
#define SFNT_OFFSET_TABLE     12
#define SFNT_RECORD_SIZE      16
#define WOFF_MAX_TABLE_BYTES  (256u * 1024u * 1024u)

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}

static size_t align4(size_t n)
{
    return (n + 3u) & ~(size_t)3u;
}

bool woff_is_woff(const uint8_t *data, size_t size)
{
    return data && size >= WOFF_HEADER_SIZE && memcmp(data, "wOFF", 4) == 0;
}

uint8_t *woff_to_sfnt(const uint8_t *data, size_t size, size_t *out_size)
{
    if (!woff_is_woff(data, size) || !out_size) return NULL;

    uint32_t flavor     = rd32(data + 4);
    uint16_t num_tables = rd16(data + 12);
    if (num_tables == 0) return NULL;

    const uint8_t *dir = data + WOFF_HEADER_SIZE;
    size_t dir_bytes = (size_t)num_tables * WOFF_DIR_ENTRY_SIZE;
    if (dir_bytes > size - WOFF_HEADER_SIZE) return NULL;

    /* Output: offset table, table records, then 4-byte-aligned table data. */
    size_t total = SFNT_OFFSET_TABLE + (size_t)num_tables * SFNT_RECORD_SIZE;
    for (uint16_t i = 0; i < num_tables; i++) {
        const uint8_t *e = dir + (size_t)i * WOFF_DIR_ENTRY_SIZE;
        uint32_t offset = rd32(e + 4);
        uint32_t comp   = rd32(e + 8);
        uint32_t orig   = rd32(e + 12);
        if (orig > WOFF_MAX_TABLE_BYTES) return NULL;
        if (offset > size || comp > size - offset) return NULL;
        total += align4(orig);
    }

    uint8_t *out = calloc(total, 1);
    if (!out) return NULL;

    /* Offset table: the binary-search helpers are required by the format. */
    uint16_t entry_selector = 0;
    uint16_t search_range = 1;
    while ((uint32_t)search_range * 2u <= num_tables) {
        search_range = (uint16_t)(search_range * 2u);
        entry_selector++;
    }
    search_range = (uint16_t)(search_range * 16u);
    wr32(out, flavor);
    wr16(out + 4, num_tables);
    wr16(out + 6, search_range);
    wr16(out + 8, entry_selector);
    wr16(out + 10, (uint16_t)(num_tables * 16u - search_range));

    size_t table_off = SFNT_OFFSET_TABLE + (size_t)num_tables * SFNT_RECORD_SIZE;
    for (uint16_t i = 0; i < num_tables; i++) {
        const uint8_t *e = dir + (size_t)i * WOFF_DIR_ENTRY_SIZE;
        uint32_t tag      = rd32(e);
        uint32_t offset   = rd32(e + 4);
        uint32_t comp     = rd32(e + 8);
        uint32_t orig     = rd32(e + 12);
        uint32_t checksum = rd32(e + 16);

        uint8_t *rec = out + SFNT_OFFSET_TABLE + (size_t)i * SFNT_RECORD_SIZE;
        wr32(rec, tag);
        wr32(rec + 4, checksum);
        wr32(rec + 8, (uint32_t)table_off);
        wr32(rec + 12, orig);

        if (comp < orig) {
            /* zlib stream (with header) that inflates to exactly orig bytes. */
            int outlen = 0;
            char *dec = stbi_zlib_decode_malloc_guesssize_headerflag(
                (const char *)(data + offset), (int)comp, (int)orig, &outlen, 1);
            if (!dec || outlen < 0 || (uint32_t)outlen != orig) {
                free(dec);
                free(out);
                return NULL;
            }
            memcpy(out + table_off, dec, orig);
            free(dec);
        } else {
            /* Stored uncompressed (the format requires comp == orig then). */
            if (comp != orig) {
                free(out);
                return NULL;
            }
            memcpy(out + table_off, data + offset, orig);
        }
        table_off += align4(orig);
    }

    *out_size = total;
    return out;
}
