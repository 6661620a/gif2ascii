#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

/* Escape sequences */
#define ESC_HIDE_CURSOR  "\033[?25l"
#define ESC_SHOW_CURSOR  "\033[?25h"
#define ESC_CLEAR        "\033[2J"
#define ESC_HOME         "\033[H"
#define ESC_RESET        "\033[0m"

/* Signal flag; using volatile sig_atomic_t is correct per POSIX. */
static volatile sig_atomic_t g_stop = 0;

static void sig_handler(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void restore_terminal(void)
{
    /* Restore cursor and reset attributes regardless of how we exit. */
    const char *seq = ESC_SHOW_CURSOR ESC_RESET "\n";
    ssize_t r = write(STDOUT_FILENO, seq, strlen(seq));
    (void)r; /* nothing we can do if this fails */
}

static void sleep_cs(int cs, double time_scale)
{
    /* cs is in centiseconds (1/100 s); minimum 1 ms after scaling. */
    long ms = (long)((double)cs * 10.0 / time_scale);
    if (ms < 1)
        ms = 1;

    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* Write exactly n bytes, retrying on EINTR / partial writes. */
static void write_all(int fd, const char *buf, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, buf, n);
        if (w <= 0)
            return;
        buf += w;
        n   -= (size_t)w;
    }
}

void render_play(const ascii_frames_t *frames, int loop_count,
                 double time_scale)
{
    if (!frames || frames->frame_count == 0)
        return;
    if (time_scale <= 0.0)
        time_scale = 1.0;

    /* Install signal handlers so we can clean up on Ctrl-C / termination. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Register atexit cleanup so the cursor is restored even on exit(). */
    atexit(restore_terminal);

    /* Hide cursor and clear screen once at the start. */
    write_all(STDOUT_FILENO, ESC_HIDE_CURSOR, strlen(ESC_HIDE_CURSOR));
    write_all(STDOUT_FILENO, ESC_CLEAR,       strlen(ESC_CLEAR));

    int infinite    = (loop_count == 0);
    int loops_done  = 0;

    while (!g_stop && (infinite || loops_done < loop_count)) {
        for (int i = 0; i < frames->frame_count && !g_stop; i++) {
            const ascii_frame_t *f = &frames->frames[i];

            /* Reposition cursor to top-left without clearing (less flicker). */
            write_all(STDOUT_FILENO, ESC_HOME, strlen(ESC_HOME));

            /* Write the entire pre-rendered frame in one burst. */
            write_all(STDOUT_FILENO, f->buf, f->len);

            sleep_cs(f->delay_cs, time_scale);
        }
        loops_done++;
    }

    /* Restore cursor (atexit will also do this, but be explicit). */
    write_all(STDOUT_FILENO, ESC_SHOW_CURSOR ESC_RESET "\n",
              strlen(ESC_SHOW_CURSOR ESC_RESET "\n"));
}
