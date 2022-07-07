#ifndef BSTGW_FEC_UTILS_H
#define BSTGW_FEC_UTILS_H

#include <stdlib.h>
#include <mm/core_mmu.h>

#ifndef unlikely
#define unlikely(x)      __builtin_expect(!!(x), 0)
#endif

#ifndef likely
#define likely(x)      __builtin_expect(!!(x), 1)
#endif

// taken from linux/err.h
#define MAX_ERRNO   4095
#define IS_ERR_VALUE(x) unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)
static inline bool IS_ERR(const void *ptr) {
    return IS_ERR_VALUE((unsigned long)ptr);
}

// taken from linux/kernel.h
/*
 * This looks more complex than it should be. But we need to
 * get the type for the ~ right in round_down (it needs to be
 * as wide as the result!), and we want to evaluate the macro
 * arguments just once each.
 */
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
#define round_down(x, y) ((x) & ~__round_mask(x, y))

// linux/slab.h
#define GFP_KERNEL (0)
static inline void linux_kfree(void *p) { free(p); }
static inline void *linux_kmalloc(size_t size, unsigned flags __unused) { return malloc(size); }
static inline void *linux_kzalloc(size_t size, unsigned flags __unused) { return calloc(1, size); }

/* helper functions */
struct ifnet;
struct ifnet * bstgw_alloc_etherdev(size_t);

struct device;
struct resource;
enum resource_type;

struct resource * bstgw_platform_get_resource(
    struct device *, enum resource_type, unsigned int);

vaddr_t bstgw_devm_ioremap_resource(struct resource *);


#if defined(ARM32)
#define BITS_PER_LONG 32
#elif defined(ARM64)
#define BITS_PER_LONG 64
#else
#error Unknown BITS_PER_LONG
#endif

// TODO: from Linux kernel __fls.h
static __always_inline unsigned long __fls(unsigned long word)
{
	int num = BITS_PER_LONG - 1;

#if BITS_PER_LONG == 64
	if (!(word & (~0ul << 32))) {
		num -= 32;
		word <<= 32;
	}
#endif
	if (!(word & (~0ul << (BITS_PER_LONG-16)))) {
		num -= 16;
		word <<= 16;
	}
	if (!(word & (~0ul << (BITS_PER_LONG-8)))) {
		num -= 8;
		word <<= 8;
	}
	if (!(word & (~0ul << (BITS_PER_LONG-4)))) {
		num -= 4;
		word <<= 4;
	}
	if (!(word & (~0ul << (BITS_PER_LONG-2)))) {
		num -= 2;
		word <<= 2;
	}
	if (!(word & (~0ul << (BITS_PER_LONG-1))))
		num -= 1;
	return num;
}

#endif /* BSTGW_FEC_UTILS_H */