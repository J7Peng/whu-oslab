#ifndef _STRING_H__
#define _STRING_H__

#include "common.h"

void *memset(void *dst, int c, int64 n);
void *memmove(void *dst, const void *src, int64 n);
int   memcmp(const void *a, const void *b, int64 n);
void *memcpy(void* dst, const void* src, uint64 n);
int   strlen(const char *s);

#endif