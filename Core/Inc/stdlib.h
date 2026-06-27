#ifndef __MINIMAL_STDLIB_H
#define __MINIMAL_STDLIB_H

#include <stddef.h>

#define NULL ((void *)0)

void abort(void) __attribute__((noreturn));

#endif
