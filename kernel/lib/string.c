#include <ck/string.h>
#include <ck/types.h>

void *memset(void *dst, int c, size_t n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--)
        *p++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0)
        return dst;

    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    while (n--) {
        if (*pa != *pb)
            return (int)*pa - (int)*pb;
        pa++;
        pb++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++))
        ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    char *d = dst;
    /* Copy src bytes (including '\0') while n > 0 */
    while (n && (*d = *src)) {
        d++;
        src++;
        n--;
    }
    /* Zero-pad remaining bytes */
    while (n--)
        *d++ = '\0';
    return dst;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d)
        d++;
    while ((*d++ = *src++))
        ;
    return dst;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if ((unsigned char)*s == (unsigned char)c)
            return (char *)s;
        s++;
    }
    if (c == '\0')
        return (char *)s;
    return NULL;
}

/* ── Number-to-string helpers ────────────────────────────────────────── */

int itoa_dec(s64 val, char *buf)
{
    char tmp[22];
    int  i = 0, neg = 0, n;
    u64  uval;

    if (val < 0) {
        neg  = 1;
        uval = (u64)(-(val + 1)) + 1ULL; /* avoid UB for LLONG_MIN */
    } else {
        uval = (u64)val;
    }

    if (uval == 0) {
        tmp[i++] = '0';
    } else {
        while (uval) {
            tmp[i++] = (char)('0' + uval % 10);
            uval /= 10;
        }
    }

    n = 0;
    if (neg)
        buf[n++] = '-';
    while (i--)
        buf[n++] = tmp[i];
    buf[n] = '\0';
    return n;
}

int utoa_dec(u64 val, char *buf)
{
    char tmp[22];
    int  i = 0, n;

    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val) {
            tmp[i++] = (char)('0' + val % 10);
            val /= 10;
        }
    }

    n = 0;
    while (i--)
        buf[n++] = tmp[i];
    buf[n] = '\0';
    return n;
}

int utoa_hex(u64 val, char *buf, int upper)
{
    static const char lo[] = "0123456789abcdef";
    static const char hi[] = "0123456789ABCDEF";
    const char *digits = upper ? hi : lo;
    char tmp[17];
    int  i = 0, n;

    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val) {
            tmp[i++] = digits[val & 0xf];
            val >>= 4;
        }
    }

    n = 0;
    while (i--)
        buf[n++] = tmp[i];
    buf[n] = '\0';
    return n;
}
