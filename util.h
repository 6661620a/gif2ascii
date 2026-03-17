#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

void *xmalloc(size_t n);
void *xcalloc(size_t nmemb, size_t size);
void *xrealloc(void *ptr, size_t n);

#endif /* UTIL_H */
