#ifndef CK_STRING_H
#define CK_STRING_H

#include <ck/types.h>

/* Memory operations */
void  *memset(void *dst, int c, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *a, const void *b, size_t n);

/* String operations */
size_t strlen(const char *s);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcat(char *dst, const char *src);
char  *strchr(const char *s, int c);

/* Number to string helpers (used by printk) */
int    itoa_dec(s64 val, char *buf);   /* returns chars written */
int    utoa_dec(u64 val, char *buf);
int    utoa_hex(u64 val, char *buf, int upper);

/* Substring search */
char  *strstr(const char *haystack, const char *needle);

#endif /* CK_STRING_H */
