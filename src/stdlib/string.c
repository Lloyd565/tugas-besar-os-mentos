#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "header/stdlib/string.h"

void* memset(void *s, int c, size_t n) {
    uint8_t *buf = (uint8_t*) s;
    for (size_t i = 0; i < n; i++)
        buf[i] = (uint8_t) c;
    return s;
}

void* memcpy(void* restrict dest, const void* restrict src, size_t n) {
    uint8_t *dstbuf       = (uint8_t*) dest;
    const uint8_t *srcbuf = (const uint8_t*) src;
    for (size_t i = 0; i < n; i++)
        dstbuf[i] = srcbuf[i];
    return dstbuf;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *buf1 = (const uint8_t*) s1;
    const uint8_t *buf2 = (const uint8_t*) s2;
    for (size_t i = 0; i < n; i++) {
        if (buf1[i] < buf2[i])
            return -1;
        else if (buf1[i] > buf2[i])
            return 1;
    }

    return 0;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *dstbuf       = (uint8_t*) dest;
    const uint8_t *srcbuf = (const uint8_t*) src;
    if (dstbuf < srcbuf) {
        for (size_t i = 0; i < n; i++)
            dstbuf[i]   = srcbuf[i];
    } else {
        for (size_t i = n; i != 0; i--)
            dstbuf[i-1] = srcbuf[i-1];
    }

    return dest;
}


size_t strlen(const char *s) {
    const char *p = s;
    while (*p)
    {
        p++;
    }
    return (size_t)(p - s);
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return (int)((unsigned char)*s1 - (unsigned char)*s2);
}

void strcpy(char *dest, const char *src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

void strcat(char *dest, const char *src) {
    while (*dest) dest++;
    while (*src) *dest++ = *src++;
    *dest = '\0';
}

int snprintf(char *str, size_t size, const char *format, ...) {
    if (!str || !format || size == 0) return 0;
    
    size_t len = 0;
    const char *ptr = format;
    char *strptr = str;
    
    va_list args;
    va_start(args, format);
    
    while (*ptr && len < size - 1) {
        if (*ptr == '%') {
            ptr++;
            if (*ptr == 's') {
                const char *s = va_arg(args, const char *);
                while (*s && len < size - 1) {
                    *strptr++ = *s++;
                    len++;
                }
            }
        } else {
            *strptr++ = *ptr;
            len++;
        }
        ptr++;
    }
    
    *strptr = '\0';
    va_end(args);
    return len;
}