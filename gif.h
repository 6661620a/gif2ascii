#ifndef GIF_H
#define GIF_H

#include <stdint.h>
#include <stddef.h>

/* A single decoded GIF frame: full RGBA canvas at screen resolution. */
typedef struct {
    int      delay_cs;   /* frame delay in centiseconds (1/100 s) */
    uint8_t *pixels;     /* screen_width * screen_height * 4 bytes (RGBA) */
} gif_frame_t;

typedef struct {
    int          width;
    int          height;
    int          frame_count;
    int          loop_count;  /* 0 = infinite, >0 = play that many times */
    gif_frame_t *frames;
} gif_image_t;

gif_image_t *gif_load(const char *filename);
void         gif_free(gif_image_t *gif);

#endif /* GIF_H */
