/*
 * mm_os.c - OS-specific memory primitives.
 *
 * Three backends, chosen at compile time:
 *
 *   1. Windows          -> VirtualAlloc / VirtualFree   (default on _WIN32)
 *   2. POSIX + sbrk     -> the article's approach        (-DMYMALLOC_BACKEND_SBRK)
 *   3. POSIX + mmap     -> default, safe next to libc
 *
 * Why mmap is the default on POSIX: the C library uses sbrk() for its own
 * heap. Moving the program break behind its back corrupts libc's view of the
 * heap, so the moment you call printf() or fopen() the program can die in
 * confusing ways. mmap() gives us a private arena that libc never touches.
 *
 * The sbrk backend is kept because it is what the article does, and because
 * seeing the conflict happen is instructive. Build it with:
 *     cmake -DMYMALLOC_USE_SBRK=ON ..
 */
#include "mm_os.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void *mm_os_reserve(size_t bytes)
{
    return VirtualAlloc(NULL, bytes, MEM_RESERVE, PAGE_NOACCESS);
}

bool mm_os_commit(void *base, size_t offset, size_t length)
{
    void *p = VirtualAlloc((char *)base + offset, length, MEM_COMMIT,
                           PAGE_READWRITE);
    return p != NULL;
}

void mm_os_decommit(void *base, size_t offset, size_t length)
{
    VirtualFree((char *)base + offset, length, MEM_DECOMMIT);
}

void mm_os_release(void *base, size_t bytes)
{
    (void)bytes;
    VirtualFree(base, 0, MEM_RELEASE);
}

void mm_os_yield(void)
{
    SwitchToThread();
}

#elif defined(MYMALLOC_BACKEND_SBRK)

#include <sched.h>
#include <stdint.h>
#include <unistd.h>

void *mm_os_reserve(size_t bytes)
{
    (void)bytes;
    /* sbrk(0) returns the current end of the heap: that is our base. */
    return sbrk(0);
}

bool mm_os_commit(void *base, size_t offset, size_t length)
{
    (void)base;
    (void)offset;
    return sbrk((intptr_t)length) != (void *)-1;
}

void mm_os_decommit(void *base, size_t offset, size_t length)
{
    (void)base;
    (void)offset;
    (void)sbrk(-(intptr_t)length);
}

void mm_os_release(void *base, size_t bytes)
{
    (void)base;
    (void)bytes;
}

void mm_os_yield(void)
{
    sched_yield();
}

#else /* POSIX + mmap */

#include <sched.h>
#include <sys/mman.h>

void *mm_os_reserve(size_t bytes)
{
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

bool mm_os_commit(void *base, size_t offset, size_t length)
{
    /* The region is already PROT_READ|PROT_WRITE: pages are committed by the
     * kernel on first touch, so there is nothing to do here. */
    (void)base;
    (void)offset;
    (void)length;
    return true;
}

void mm_os_decommit(void *base, size_t offset, size_t length)
{
    /* MADV_DONTNEED returns the pages to the OS while keeping the mapping.
     * The next access yields a fresh zero page, exactly like sbrk(-n). */
    madvise((char *)base + offset, length, MADV_DONTNEED);
}

void mm_os_release(void *base, size_t bytes)
{
    munmap(base, bytes);
}

void mm_os_yield(void)
{
    sched_yield();
}

#endif
