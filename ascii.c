#include "ascii.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * ASCII luminance ramp, darkest (index 0) to brightest (last index).
 * Adjust freely; just keep RAMP_LEN in sync.
 */
static const char RAMP[] = " .:-=+*#%@";
#define RAMP_LEN ((int)(sizeof(RAMP) - 1))

/* Perceptual grayscale conversion, result clamped to 0-255. */
static uint8_t to_gray(uint8_t r, uint8_t g, uint8_t b)
{
    double v = 0.299 * r + 0.587 * g + 0.114 * b + 0.5;
    if (v > 255.0) v = 255.0;
    return (uint8_t)v;
}

/*
 * Nearest-neighbour RGBA resize.
 * Returns a newly allocated buffer of dw*dh*4 bytes.
 */
static uint8_t *resize_rgba(const uint8_t *src, int sw, int sh,
                             int dw, int dh)
{
    uint8_t *dst = xmalloc((size_t)dw * (size_t)dh * 4);
    for (int dy = 0; dy < dh; dy++) {
        int sy = dy * sh / dh;
        for (int dx = 0; dx < dw; dx++) {
            int sx = dx * sw / dw;
            const uint8_t *sp = src + ((size_t)sy * (size_t)sw + (size_t)sx) * 4;
            uint8_t       *dp = dst + ((size_t)dy * (size_t)dw + (size_t)dx) * 4;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }
    return dst;
}

ascii_frames_t *ascii_build_frames(const gif_image_t *gif,
                                   int target_width, int color)
{
    if (!gif || gif->frame_count == 0 || target_width <= 0)
        return NULL;

    int src_w  = gif->width;
    int src_h  = gif->height;
    int char_w = target_width;
    /*
     * Terminal characters are roughly 2× taller than wide, so halve the
     * row count to preserve the original aspect ratio.
     */
    int char_h = (src_w > 0)
        ? (int)((double)src_h / (double)src_w * (double)char_w * 0.5 + 0.5)
        : 1;
    if (char_h < 1)
        char_h = 1;

    ascii_frames_t *af = xcalloc(1, sizeof(*af));
    af->frame_count = gif->frame_count;
    af->char_width  = char_w;
    af->char_height = char_h;
    af->color       = color;
    af->frames      = xmalloc((size_t)gif->frame_count * sizeof(ascii_frame_t));

    for (int i = 0; i < gif->frame_count; i++) {
        const gif_frame_t *gf  = &gif->frames[i];
        ascii_frame_t     *aff = &af->frames[i];

        /* Scale the RGBA canvas down to the character grid dimensions */
        uint8_t *small = resize_rgba(gf->pixels, src_w, src_h, char_w, char_h);

        /*
         * Estimate output buffer size:
         *   Plain:  char_w chars + 1 newline per row
         *   Color:  up to ~22 bytes per char ("\033[38;2;255;255;255mX")
         *           plus a 4-byte reset ("\033[0m") per row
         */
        size_t row_cap = color
            ? (size_t)char_w * 24 + 8
            : (size_t)char_w + 2;
        size_t buf_cap = (size_t)char_h * row_cap + 8;
        char  *buf     = xmalloc(buf_cap);
        size_t pos     = 0;

        for (int y = 0; y < char_h; y++) {
            for (int x = 0; x < char_w; x++) {
                const uint8_t *px =
                    small + ((size_t)y * (size_t)char_w + (size_t)x) * 4;

                uint8_t gray = to_gray(px[0], px[1], px[2]);
                int     idx  = (int)gray * (RAMP_LEN - 1) / 255;
                char    ch   = RAMP[idx];

                if (color) {
                    /* Grow buffer if needed (ANSI sequence is at most ~22 B) */
                    if (pos + 32 > buf_cap) {
                        buf_cap  = buf_cap * 2 + 256;
                        buf      = xrealloc(buf, buf_cap);
                    }
                    /* ANSI 24-bit foreground colour */
                    int w = snprintf(buf + pos, buf_cap - pos,
                                     "\033[38;2;%u;%u;%um%c",
                                     (unsigned)px[0],
                                     (unsigned)px[1],
                                     (unsigned)px[2],
                                     ch);
                    if (w > 0)
                        pos += (size_t)w;
                } else {
                    if (pos + 2 > buf_cap) {
                        buf_cap  = buf_cap * 2;
                        buf      = xrealloc(buf, buf_cap);
                    }
                    buf[pos++] = ch;
                }
            }

            /* End of row: reset colour (color mode) then newline */
            if (color) {
                if (pos + 8 > buf_cap) {
                    buf_cap += 64;
                    buf      = xrealloc(buf, buf_cap);
                }
                /* SGR reset */
                buf[pos++] = '\033';
                buf[pos++] = '[';
                buf[pos++] = '0';
                buf[pos++] = 'm';
            }
            if (pos + 2 > buf_cap) {
                buf_cap += 16;
                buf      = xrealloc(buf, buf_cap);
            }
            buf[pos++] = '\n';
        }

        free(small);

        aff->buf      = buf;
        aff->len      = pos;
        aff->delay_cs = gf->delay_cs;
    }

    return af;
}

void ascii_frames_free(ascii_frames_t *af)
{
    if (!af)
        return;
    for (int i = 0; i < af->frame_count; i++)
        free(af->frames[i].buf);
    free(af->frames);
    free(af);
}
