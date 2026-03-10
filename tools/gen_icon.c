/* gen_icon.c — Build-time tool to generate azmap icon as PNG.
 * Usage: gen_icon <size> <output.png>
 * Uses the same icon_generate_sz() from src/icon.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "../src/icon.h"

/* Minimal PNG writer (uncompressed, using stored zlib blocks) */

static uint32_t crc_table[256];
static int crc_table_ready = 0;

static void make_crc_table(void)
{
    for (int n = 0; n < 256; n++) {
        uint32_t c = (uint32_t)n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xedb88320UL ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_table_ready = 1;
}

static uint32_t update_crc(uint32_t crc, const unsigned char *buf, int len)
{
    if (!crc_table_ready) make_crc_table();
    uint32_t c = crc;
    for (int n = 0; n < len; n++)
        c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
    return c;
}

static uint32_t png_crc(const unsigned char *buf, int len)
{
    return update_crc(0xffffffffUL, buf, len) ^ 0xffffffffUL;
}

static uint32_t adler32_calc(const unsigned char *buf, int len)
{
    uint32_t a = 1, b = 0;
    for (int i = 0; i < len; i++) {
        a = (a + buf[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff;
    p[3] = v & 0xff;
}

static void put16le(unsigned char *p, uint16_t v)
{
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
}

static void write_chunk(FILE *f, const char *type, const unsigned char *data, int len)
{
    unsigned char hdr[8];
    put32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len > 0) fwrite(data, 1, len, f);
    unsigned char *crc_buf = malloc(4 + len);
    if (!crc_buf) return;
    memcpy(crc_buf, type, 4);
    if (len > 0) memcpy(crc_buf + 4, data, len);
    uint32_t crc = png_crc(crc_buf, 4 + len);
    free(crc_buf);
    unsigned char crc_bytes[4];
    put32(crc_bytes, crc);
    fwrite(crc_bytes, 1, 4, f);
}

static int write_png(const char *path, const unsigned char *pixels, int sz)
{
    int row_bytes = sz * 4;
    int raw_len = sz * (1 + row_bytes);
    unsigned char *raw = malloc(raw_len);
    for (int y = 0; y < sz; y++) {
        raw[y * (1 + row_bytes)] = 0;
        memcpy(raw + y * (1 + row_bytes) + 1, pixels + y * row_bytes, row_bytes);
    }

    /* Zlib stored blocks */
    int remaining = raw_len;
    int zlib_size = 2 + 4;
    int tmp = remaining;
    while (tmp > 0) {
        int blk = (tmp > 65535) ? 65535 : tmp;
        zlib_size += 5 + blk;
        tmp -= blk;
    }
    unsigned char *zlib = malloc(zlib_size);
    int zpos = 0;
    zlib[zpos++] = 0x78;
    zlib[zpos++] = 0x01;
    int offset = 0;
    remaining = raw_len;
    while (remaining > 0) {
        int blk = (remaining > 65535) ? 65535 : remaining;
        int last = (remaining <= 65535) ? 1 : 0;
        zlib[zpos++] = last;
        put16le(zlib + zpos, (uint16_t)blk); zpos += 2;
        put16le(zlib + zpos, (uint16_t)(~blk)); zpos += 2;
        memcpy(zlib + zpos, raw + offset, blk);
        zpos += blk;
        offset += blk;
        remaining -= blk;
    }
    uint32_t adl = adler32_calc(raw, raw_len);
    put32(zlib + zpos, adl);
    zpos += 4;

    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(zlib); return -1; }

    unsigned char sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);

    unsigned char ihdr[13];
    put32(ihdr, sz);
    put32(ihdr + 4, sz);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    write_chunk(f, "IHDR", ihdr, 13);
    write_chunk(f, "IDAT", zlib, zpos);
    write_chunk(f, "IEND", NULL, 0);

    fclose(f);
    free(raw);
    free(zlib);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: gen_icon <size> <output.png>\n");
        return 1;
    }
    int sz = atoi(argv[1]);
    if (sz < 1 || sz > 1024) {
        fprintf(stderr, "Invalid size %d (must be 1–1024)\n", sz);
        return 1;
    }
    const char *outpath = argv[2];

    unsigned char *pixels = malloc(sz * sz * 4);
    if (!pixels) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    icon_generate_sz(pixels, sz);

    if (write_png(outpath, pixels, sz) < 0) {
        fprintf(stderr, "Cannot open %s\n", outpath);
        free(pixels);
        return 1;
    }
    free(pixels);
    printf("Generated %s (%dx%d)\n", outpath, sz, sz);
    return 0;
}