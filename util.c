#include "util.h"
#include <stdio.h>
#include <stdlib.h>

void *xmalloc(size_t n)
{
    if (n == 0)
        n = 1;
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return p;
}

void *xcalloc(size_t nmemb, size_t size)
{
    if (nmemb == 0)
        nmemb = 1;
    void *p = calloc(nmemb, size);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return p;
}

void *xrealloc(void *ptr, size_t n)
{
    if (n == 0)
        n = 1;
    void *p = realloc(ptr, n);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return p;
}
