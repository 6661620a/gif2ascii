#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "gif.h"
#include "ascii.h"
#include "render.h"

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS] input.gif\n"
        "\n"
        "Options:\n"
        "  -w, --width <cols>   Target character width "
                               "(default: terminal width)\n"
        "  -c, --color          Enable ANSI 24-bit colour output\n"
        "  -l, --loop <n>       Loop count: 0 = infinite, default = GIF value\n"
        "  -s, --scale <f>      Timing scale factor (>1 faster, <1 slower; "
                               "default: 1.0)\n"
        "  -h, --help           Show this help\n",
        prog);
}

static int terminal_width(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return (int)ws.ws_col;
    return 80;
}

int main(int argc, char **argv)
{
    static const struct option long_opts[] = {
        { "width",  required_argument, NULL, 'w' },
        { "color",  no_argument,       NULL, 'c' },
        { "loop",   required_argument, NULL, 'l' },
        { "scale",  required_argument, NULL, 's' },
        { "help",   no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int    target_width = 0;
    int    color        = 0;
    int    loop_count   = -1;   /* -1 means "use the GIF's own value" */
    double time_scale   = 1.0;

    int opt;
    while ((opt = getopt_long(argc, argv, "w:cl:s:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'w':
            target_width = atoi(optarg);
            if (target_width <= 0) {
                fprintf(stderr, "gif2ascii: invalid width '%s'\n", optarg);
                return 1;
            }
            break;
        case 'c':
            color = 1;
            break;
        case 'l':
            loop_count = atoi(optarg);
            if (loop_count < 0) {
                fprintf(stderr, "gif2ascii: invalid loop count '%s'\n", optarg);
                return 1;
            }
            break;
        case 's':
            time_scale = atof(optarg);
            if (time_scale <= 0.0) {
                fprintf(stderr, "gif2ascii: invalid scale '%s'\n", optarg);
                return 1;
            }
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "gif2ascii: no input file specified\n");
        usage(argv[0]);
        return 1;
    }

    if (target_width == 0)
        target_width = terminal_width();

    /* ---- Load ---- */
    const char *filename = argv[optind];
    gif_image_t *gif = gif_load(filename);
    if (!gif)
        return 1;

    if (gif->frame_count == 0) {
        fprintf(stderr, "gif2ascii: no frames decoded from '%s'\n", filename);
        gif_free(gif);
        return 1;
    }

    /* Capture the GIF's loop count before we free it. */
    int gif_loops = gif->loop_count;

    /* ---- Convert to ASCII ---- */
    ascii_frames_t *frames = ascii_build_frames(gif, target_width, color);
    gif_free(gif);
    if (!frames) {
        fprintf(stderr, "gif2ascii: failed to build ASCII frames\n");
        return 1;
    }

    /* CLI -l overrides the GIF's own loop directive. */
    int play_loops = (loop_count >= 0) ? loop_count : gif_loops;

    render_play(frames, play_loops, time_scale);

    ascii_frames_free(frames);
    return 0;
}
