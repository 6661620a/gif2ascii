#include "gif.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Bit-reader for packed LZW bitstream
 * ----------------------------------------------------------------------- */
typedef struct {
    const uint8_t *data;
    size_t         size;
    size_t         pos;
    uint32_t       bits;
    int            nbits;
} bit_reader_t;

static void br_init(bit_reader_t *br, const uint8_t *data, size_t size)
{
    br->data  = data;
    br->size  = size;
    br->pos   = 0;
    br->bits  = 0;
    br->nbits = 0;
}

static int br_read(bit_reader_t *br, int n)
{
    while (br->nbits < n) {
        if (br->pos >= br->size)
            return -1;
        br->bits  |= (uint32_t)br->data[br->pos++] << br->nbits;
        br->nbits += 8;
    }
    int val    = (int)(br->bits & ((1u << n) - 1));
    br->bits >>= n;
    br->nbits -= n;
    return val;
}

/* -----------------------------------------------------------------------
 * LZW decoder (GIF variant)
 *
 * Each table entry has (prefix, suffix).
 * For root codes (< clear_code): prefix = -1, suffix = literal value.
 * For derived codes: the decoded string is string(prefix) + suffix.
 *
 * The stack stores a decoded string in REVERSE order so that popping
 * from top yields characters in forward (correct) order.
 * ----------------------------------------------------------------------- */
#define LZW_MAX_CODES 4096

typedef struct {
    int     prefix;  /* -1 for root entries */
    uint8_t suffix;
} lzw_entry_t;

typedef struct {
    lzw_entry_t table[LZW_MAX_CODES];
    uint8_t     stack[LZW_MAX_CODES + 1]; /* +1 for KwKwK insert */
    int         stack_top;
    int         min_size;
    int         clear_code;
    int         eoi_code;
    int         next_code;
    int         code_size;
} lzw_t;

static void lzw_reset(lzw_t *lzw)
{
    int i;
    for (i = 0; i < lzw->clear_code; i++) {
        lzw->table[i].prefix = -1;
        lzw->table[i].suffix = (uint8_t)i;
    }
    lzw->next_code = lzw->eoi_code + 1;
    lzw->code_size = lzw->min_size + 1;
    lzw->stack_top = 0;
}

static void lzw_init(lzw_t *lzw, int min_size)
{
    lzw->min_size   = min_size;
    lzw->clear_code = 1 << min_size;
    lzw->eoi_code   = lzw->clear_code + 1;
    lzw_reset(lzw);
}

/*
 * Follow code's prefix chain to the root, pushing suffix values onto the
 * stack so that the TOP of the stack holds the FIRST (root) character.
 * Returns the first character of the decoded string, or -1 on error.
 */
static int lzw_push(lzw_t *lzw, int code)
{
    int depth = 0;
    int c     = code;
    while (lzw->table[c].prefix != -1) {
        if (lzw->stack_top >= LZW_MAX_CODES || ++depth > LZW_MAX_CODES)
            return -1;
        lzw->stack[lzw->stack_top++] = lzw->table[c].suffix;
        c = lzw->table[c].prefix;
    }
    if (lzw->stack_top >= LZW_MAX_CODES)
        return -1;
    lzw->stack[lzw->stack_top++] = lzw->table[c].suffix;
    return (int)lzw->table[c].suffix; /* first character */
}

/*
 * Decode the LZW sub-block data in [src, src+src_len) into dst[0..dst_len).
 * Returns the number of pixels written.
 */
static int lzw_decode(const uint8_t *src, size_t src_len,
                      uint8_t *dst, int dst_len, int min_size)
{
    if (min_size < 2 || min_size > 11)
        return 0;

    lzw_t        lzw;
    bit_reader_t br;
    int          written   = 0;
    int          prev_code = -1;

    lzw_init(&lzw, min_size);
    br_init(&br, src, src_len);

    for (;;) {
        int code = br_read(&br, lzw.code_size);
        if (code < 0)
            break;

        if (code == lzw.clear_code) {
            lzw_reset(&lzw);
            prev_code = -1;
            continue;
        }
        if (code == lzw.eoi_code)
            break;

        int first_pixel;

        if (code < lzw.next_code) {
            /* Code already in table */
            lzw.stack_top = 0;
            first_pixel = lzw_push(&lzw, code);
            if (first_pixel < 0)
                break;
        } else if (code == lzw.next_code && prev_code >= 0) {
            /*
             * KwKwK special case: the encoder created an entry and
             * immediately used it.  string(code) = string(prev_code)
             * followed by the first character of string(prev_code).
             *
             * Build that on the stack:
             *   1. Push string(prev_code) — first char ends up at top.
             *   2. Insert that first char at position 0 (the bottom) so
             *      it is emitted last, giving the correct output order.
             */
            lzw.stack_top = 0;
            first_pixel = lzw_push(&lzw, prev_code);
            if (first_pixel < 0)
                break;
            /* Shift everything up and place first_pixel at the bottom */
            memmove(lzw.stack + 1, lzw.stack, (size_t)lzw.stack_top);
            lzw.stack[0] = (uint8_t)first_pixel;
            lzw.stack_top++;
        } else {
            break; /* malformed stream */
        }

        /* Add new table entry based on prev_code + first_pixel. */
        if (prev_code >= 0 && lzw.next_code < LZW_MAX_CODES) {
            lzw.table[lzw.next_code].prefix = prev_code;
            lzw.table[lzw.next_code].suffix = (uint8_t)first_pixel;
            lzw.next_code++;
            if (lzw.code_size < 12 &&
                lzw.next_code == (1 << lzw.code_size))
                lzw.code_size++;
        }

        /* Drain the stack (top = first output character). */
        while (lzw.stack_top > 0 && written < dst_len)
            dst[written++] = lzw.stack[--lzw.stack_top];
        lzw.stack_top = 0;

        prev_code = code;
    }

    return written;
}

/* -----------------------------------------------------------------------
 * Minimal buffered reader for the GIF file
 * ----------------------------------------------------------------------- */
typedef struct {
    FILE   *fp;
    uint8_t *buf;     /* accumulated sub-block data */
    size_t   buf_cap;
    size_t   buf_len;
} gif_reader_t;

static void gr_free(gif_reader_t *gr)
{
    free(gr->buf);
    gr->buf     = NULL;
    gr->buf_cap = 0;
    gr->buf_len = 0;
}

static int gr_byte(gif_reader_t *gr, uint8_t *out)
{
    int c = fgetc(gr->fp);
    if (c == EOF)
        return 0;
    *out = (uint8_t)c;
    return 1;
}

static int gr_bytes(gif_reader_t *gr, void *out, size_t n)
{
    return fread(out, 1, n, gr->fp) == n;
}

static uint16_t gr_u16(gif_reader_t *gr)
{
    uint8_t lo = 0, hi = 0;
    gr_byte(gr, &lo);
    gr_byte(gr, &hi);
    return (uint16_t)(lo | (hi << 8));
}

/*
 * Read all GIF sub-blocks (length-prefixed chunks terminated by 0x00) into
 * gr->buf.  Returns the total number of data bytes read.
 */
static size_t gr_read_subblocks(gif_reader_t *gr)
{
    gr->buf_len = 0;
    uint8_t bsz;
    while (gr_byte(gr, &bsz) && bsz > 0) {
        size_t needed = gr->buf_len + bsz;
        if (needed > gr->buf_cap) {
            gr->buf_cap = needed * 2 + 256;
            gr->buf     = xrealloc(gr->buf, gr->buf_cap);
        }
        if (!gr_bytes(gr, gr->buf + gr->buf_len, bsz))
            break;
        gr->buf_len += bsz;
    }
    return gr->buf_len;
}

/* Skip all sub-blocks without accumulating data. */
static void gr_skip_subblocks(gif_reader_t *gr)
{
    uint8_t bsz;
    while (gr_byte(gr, &bsz) && bsz > 0)
        fseek(gr->fp, (long)bsz, SEEK_CUR);
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
gif_image_t *gif_load(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror(filename);
        return NULL;
    }

    gif_reader_t gr;
    memset(&gr, 0, sizeof(gr));
    gr.fp = fp;

    /* ---- Header ---- */
    char header[7];
    if (fread(header, 1, 6, fp) != 6) {
        fclose(fp);
        return NULL;
    }
    header[6] = '\0';
    if (memcmp(header, "GIF87a", 6) != 0 && memcmp(header, "GIF89a", 6) != 0) {
        fprintf(stderr, "%s: not a GIF file\n", filename);
        fclose(fp);
        return NULL;
    }

    /* ---- Logical Screen Descriptor ---- */
    uint16_t screen_w = gr_u16(&gr);
    uint16_t screen_h = gr_u16(&gr);
    uint8_t  lsd_packed, bg_idx, aspect;
    if (!gr_byte(&gr, &lsd_packed) ||
        !gr_byte(&gr, &bg_idx)     ||
        !gr_byte(&gr, &aspect)) {
        fclose(fp);
        return NULL;
    }
    if (screen_w == 0 || screen_h == 0) {
        fprintf(stderr, "%s: zero-dimension GIF\n", filename);
        fclose(fp);
        return NULL;
    }

    int has_gct  = (lsd_packed >> 7) & 1;
    int gct_size = 2 << (lsd_packed & 0x07); /* 2..256 */

    /* ---- Global Colour Table ---- */
    uint8_t gct[256 * 3];
    memset(gct, 0, sizeof(gct));
    if (has_gct) {
        int bytes = gct_size * 3;
        if (bytes > (int)sizeof(gct))
            bytes = (int)sizeof(gct);
        gr_bytes(&gr, gct, (size_t)bytes);
    }

    gif_image_t *img = xcalloc(1, sizeof(*img));
    img->width      = (int)screen_w;
    img->height     = (int)screen_h;
    img->loop_count = 1; /* default: play once */

    /* Canvas for compositing and one backup for disposal method 3 */
    size_t canvas_bytes = (size_t)screen_w * screen_h * 4;
    uint8_t *canvas      = xcalloc(canvas_bytes, 1);
    uint8_t *prev_canvas = xcalloc(canvas_bytes, 1);

    /* Per-frame Graphic Control Extension state */
    int gce_delay   = 10; /* centiseconds */
    int gce_trans   = -1; /* -1 = none */
    int gce_dispose = 0;

    int cap = 16;
    img->frames = xmalloc((size_t)cap * sizeof(gif_frame_t));

    uint8_t intro;
    while (gr_byte(&gr, &intro)) {
        if (intro == 0x3B)
            break; /* GIF trailer */

        /* ----------------------------------------------------------------
         * Extension block
         * ---------------------------------------------------------------- */
        if (intro == 0x21) {
            uint8_t label;
            if (!gr_byte(&gr, &label))
                break;

            if (label == 0xF9) {
                /* Graphic Control Extension */
                uint8_t bsz = 0, gpacked = 0, tidx = 0, term = 0;
                uint16_t delay;
                gr_byte(&gr, &bsz);      /* block size = 4 */
                gr_byte(&gr, &gpacked);
                delay = gr_u16(&gr);
                gr_byte(&gr, &tidx);
                gr_byte(&gr, &term);

                gce_delay   = (int)delay;
                gce_dispose = (gpacked >> 2) & 0x07;
                gce_trans   = (gpacked & 0x01) ? (int)tidx : -1;

            } else if (label == 0xFF) {
                /* Application Extension — look for Netscape loop count */
                uint8_t bsz = 0;
                gr_byte(&gr, &bsz);

                uint8_t app_id[11];
                memset(app_id, 0, sizeof(app_id));
                size_t to_read = (bsz < 11) ? bsz : 11;
                gr_bytes(&gr, app_id, to_read);
                if (bsz > 11)
                    fseek(fp, (long)(bsz - 11), SEEK_CUR);

                gr_read_subblocks(&gr);

                if (memcmp(app_id, "NETSCAPE2.0", 11) == 0 &&
                    gr.buf_len >= 3 && gr.buf[0] == 0x01)
                    img->loop_count = (int)(gr.buf[1] | (gr.buf[2] << 8));

            } else {
                gr_skip_subblocks(&gr);
            }
            continue;
        }

        /* ----------------------------------------------------------------
         * Image Descriptor
         * ---------------------------------------------------------------- */
        if (intro != 0x2C)
            break; /* unknown block; give up gracefully */

        uint16_t f_left   = gr_u16(&gr);
        uint16_t f_top    = gr_u16(&gr);
        uint16_t f_width  = gr_u16(&gr);
        uint16_t f_height = gr_u16(&gr);
        uint8_t  img_packed = 0;
        gr_byte(&gr, &img_packed);

        int has_lct    = (img_packed >> 7) & 1;
        int interlaced = (img_packed >> 6) & 1;
        int lct_size   = 2 << (img_packed & 0x07);

        /* Local Colour Table (falls back to global if absent) */
        uint8_t lct[256 * 3];
        memcpy(lct, gct, sizeof(lct));
        if (has_lct)
            gr_bytes(&gr, lct, (size_t)(lct_size * 3));

        /* LZW minimum code size */
        uint8_t min_code_size = 2;
        gr_byte(&gr, &min_code_size);
        if (min_code_size < 2 || min_code_size > 11)
            min_code_size = 2;

        /* Collect all sub-blocks into the reader buffer */
        gr_read_subblocks(&gr);

        /* Validate frame bounds (clamp silently) */
        int fw = (int)f_width;
        int fh = (int)f_height;
        int fx = (int)f_left;
        int fy = (int)f_top;
        if (fw == 0 || fh == 0)
            goto next_frame;
        if (fx >= (int)screen_w || fy >= (int)screen_h)
            goto next_frame;
        if (fx + fw > (int)screen_w)
            fw = (int)screen_w - fx;
        if (fy + fh > (int)screen_h)
            fh = (int)screen_h - fy;

        {
            int npixels = fw * fh;
            uint8_t *indices = xmalloc((size_t)npixels);
            lzw_decode(gr.buf, gr.buf_len, indices, npixels,
                       (int)min_code_size);

            /* De-interlace if the interlace flag is set */
            if (interlaced) {
                static const int passes[4][2] = {{0,8},{4,8},{2,4},{1,2}};
                uint8_t *deint = xmalloc((size_t)npixels);
                int src_row   = 0;
                for (int p = 0; p < 4; p++) {
                    for (int y = passes[p][0]; y < fh; y += passes[p][1]) {
                        memcpy(deint + y * fw,
                               indices + src_row * fw,
                               (size_t)fw);
                        src_row++;
                    }
                }
                free(indices);
                indices = deint;
            }

            /* Backup canvas before compositing if disposal = 3 */
            if (gce_dispose == 3)
                memcpy(prev_canvas, canvas, canvas_bytes);

            /* Composite this sub-frame rectangle onto the canvas */
            for (int y = 0; y < fh; y++) {
                int cy = fy + y;
                for (int x = 0; x < fw; x++) {
                    int   cx  = fx + x;
                    uint8_t pi = indices[y * fw + x];
                    if (gce_trans >= 0 && pi == (uint8_t)gce_trans)
                        continue; /* transparent pixel: leave canvas */
                    size_t  coff = ((size_t)cy * screen_w + cx) * 4;
                    size_t  loff = (size_t)pi * 3;
                    canvas[coff + 0] = lct[loff + 0];
                    canvas[coff + 1] = lct[loff + 1];
                    canvas[coff + 2] = lct[loff + 2];
                    canvas[coff + 3] = 255;
                }
            }
            free(indices);

            /* Store the current canvas as this frame's pixel data */
            if (img->frame_count >= cap) {
                cap      *= 2;
                img->frames = xrealloc(img->frames,
                                       (size_t)cap * sizeof(gif_frame_t));
            }
            gif_frame_t *frame = &img->frames[img->frame_count++];
            frame->delay_cs    = gce_delay > 0 ? gce_delay : 10;
            frame->pixels      = xmalloc(canvas_bytes);
            memcpy(frame->pixels, canvas, canvas_bytes);

            /* Apply disposal method for the NEXT frame */
            switch (gce_dispose) {
            case 2:
                /* Restore to background colour within the frame rectangle */
                for (int y = fy; y < fy + fh; y++) {
                    for (int x = fx; x < fx + fw; x++) {
                        size_t coff = ((size_t)y * screen_w + x) * 4;
                        if (has_gct && bg_idx < (uint8_t)gct_size) {
                            size_t goff = (size_t)bg_idx * 3;
                            canvas[coff + 0] = gct[goff + 0];
                            canvas[coff + 1] = gct[goff + 1];
                            canvas[coff + 2] = gct[goff + 2];
                            canvas[coff + 3] = 255;
                        } else {
                            canvas[coff + 0] = 0;
                            canvas[coff + 1] = 0;
                            canvas[coff + 2] = 0;
                            canvas[coff + 3] = 0;
                        }
                    }
                }
                break;
            case 3:
                /* Restore to what was there before this frame */
                memcpy(canvas, prev_canvas, canvas_bytes);
                break;
            default:
                /* 0 or 1: leave canvas as-is */
                break;
            }
        }

next_frame:
        /* Reset GCE state for the next frame */
        gce_delay   = 10;
        gce_trans   = -1;
        gce_dispose = 0;
    }

    free(canvas);
    free(prev_canvas);
    gr_free(&gr);
    fclose(fp);
    return img;
}

void gif_free(gif_image_t *gif)
{
    if (!gif)
        return;
    for (int i = 0; i < gif->frame_count; i++)
        free(gif->frames[i].pixels);
    free(gif->frames);
    free(gif);
}
