#include "lib/str.h"

// ---------- memset ----------
void *memset(void *dst, int c, int64 n) {
    unsigned char *p = (unsigned char *)dst;
    unsigned char v = (unsigned char)c;
    for (int64 i = 0; i < n; ++i)
        p[i] = v;
    return dst;
}

// ---------- memmove ----------
void *memmove(void *dst, const void *src, int64 n) {
    const unsigned char *s = (const unsigned char *)src;
    unsigned char *d = (unsigned char *)dst;
    if (s < d && s + n > d) {  // 重叠，从后往前拷
        s += n;
        d += n;
        while (n-- > 0)
            *--d = *--s;
    } else {                   // 正常，从前往后拷
        for (int64 i = 0; i < n; ++i)
            d[i] = s[i];
    }
    return dst;
}

// ---------- memcmp ----------
int memcmp(const void *a, const void *b, int64 n) {
    const unsigned char *p1 = (const unsigned char *)a;
    const unsigned char *p2 = (const unsigned char *)b;
    for (int64 i = 0; i < n; ++i) {
        if (p1[i] != p2[i])
            return p1[i] - p2[i];
    }
    return 0;
}

void *memcpy(void* dst, const void* src, uint64 n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (uint64 i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dst;
}

// ---------- strlen ----------
int strlen(const char *s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}
