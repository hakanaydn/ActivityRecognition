#ifndef __MINIMAL_STRING_H
#define __MINIMAL_STRING_H

#include <stddef.h>

#define NULL ((void *)0)

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
size_t strlen(const char *s);

#endif
