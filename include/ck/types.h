#ifndef CK_TYPES_H
#define CK_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;

typedef int8_t    s8;
typedef int16_t   s16;
typedef int32_t   s32;
typedef int64_t   s64;

typedef s64       ssize_t;
typedef u64       phys_addr_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef __cplusplus
typedef _Bool bool;
#define true  1
#define false 0
#endif

#define PAGE_SIZE    4096ULL
#define PAGE_SHIFT   12U
#define PAGE_MASK    (~(PAGE_SIZE - 1ULL))

#define ALIGN_UP(x, a)   (((u64)(x) + (u64)(a) - 1ULL) & ~((u64)(a) - 1ULL))
#define ALIGN_DOWN(x, a) ((u64)(x) & ~((u64)(a) - 1ULL))
#define ARRAY_SIZE(a)    (sizeof(a) / sizeof((a)[0]))

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Compiler hints */
#define likely(x)    __builtin_expect(!!(x), 1)
#define unlikely(x)  __builtin_expect(!!(x), 0)
#define barrier()    __asm__ __volatile__("" ::: "memory")

#endif /* CK_TYPES_H */
