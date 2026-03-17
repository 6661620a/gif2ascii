#ifndef ASCII_H
#define ASCII_H

#include "gif.h"
#include <stddef.h>

/* One pre-rendered ASCII frame. */
typedef struct {
    char  *buf;       /* printable text; may contain ANSI escape sequences */
    size_t len;       /* byte length of buf (not NUL-terminated) */
    int    delay_cs;  /* frame delay in centiseconds */
} ascii_frame_t;

typedef struct {
    ascii_frame_t *frames;
    int            frame_count;
    int            char_width;
    int            char_height;
    int            color;
} ascii_frames_t;

/*
 * Convert all GIF frames to pre-rendered ASCII buffers.
 *
 *  target_width  - desired character column count
 *  color         - non-zero to embed ANSI 24-bit colour escape sequences
 */
ascii_frames_t *ascii_build_frames(const gif_image_t *gif,
                                   int target_width, int color);

void ascii_frames_free(ascii_frames_t *af);

#endif /* ASCII_H */
