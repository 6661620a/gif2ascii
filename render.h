#ifndef RENDER_H
#define RENDER_H

#include "ascii.h"

/*
 * Play back pre-rendered ASCII frames to stdout.
 *
 *  loop_count  - 0 = loop indefinitely; >0 = play that many full cycles
 *  time_scale  - > 1.0 speeds up, < 1.0 slows down; must be > 0
 */
void render_play(const ascii_frames_t *frames, int loop_count,
                 double time_scale);

#endif /* RENDER_H */
