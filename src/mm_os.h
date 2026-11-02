/*
 * mm_os.h - the only place where this project talks to the operating system.
 *
 * The allocator itself never calls mmap()/sbrk()/VirtualAlloc() directly.
 * It asks for a large reserved region, then commits and decommits pages
 * inside that region. Swapping backends therefore requires no change to the
 * allocator logic.
 *
 * Offsets are relative to the base pointer returned by mm_os_reserve(), so a
 * backend is free to place that region wherever it wants.
 */
#ifndef MM_OS_H
#define MM_OS_H

#include <stdbool.h>
#include <stddef.h>

/* Reserve `bytes` of address space (not necessarily backed by RAM yet).
 * Returns NULL on failure. */
void *mm_os_reserve(size_t bytes);

/* Make [base+offset, base+offset+length) readable and writable.
 * Returns false on failure. */
bool mm_os_commit(void *base, size_t offset, size_t length);

/* Give the pages in [base+offset, +length) back to the OS. The memory stays
 * reserved but any later access yields zeroed pages. */
void mm_os_decommit(void *base, size_t offset, size_t length);

/* Release the whole reserved region. */
void mm_os_release(void *base, size_t bytes);

/* Yield the CPU, used by the spin lock. */
void mm_os_yield(void);

#endif /* MM_OS_H */
