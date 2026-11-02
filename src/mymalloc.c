/*
 * mymalloc.c - a doubly linked list allocator, rewritten from the article.
 *
 * Heap layout
 * -----------
 *
 *   g_heap
 *     |
 *     v
 *   +----------------+------------+---------+------------+---------+-----+
 *   | heap header 32 | block hdr  | payload | block hdr  | payload | ... |
 *   | magic, counters| 32 bytes   |  n      | 32 bytes   |  n      |     |
 *   +----------------+------------+---------+------------+---------+-----+
 *
 *   The heap header is not part of the list.
 *   Every free block is on the list; the list is contiguous in memory, so
 *   a block always starts right after the previous payload. That invariant is
 *   what mm_check() verifies.
 *
 * Algorithm
 * ---------
 *   my_malloc  : best fit -> grow one or more pages if nothing fits -> split
 *   my_free    : merge with next, merge with previous, shrink the tail pages
 *
 * Differences with the article are listed in the README ("Ecarts assumes").
 */
#include "mymalloc.h"
#include "mm_os.h"

#include <stdalign.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define MM_PAGE_SIZE ((size_t)4096)

/* Address space we reserve up front. It costs nothing until touched. */
#define MM_HEAP_RESERVE ((size_t)256 * 1024 * 1024)

/* Every payload starts on a 16-byte boundary: malloc() must be able to hold
 * any type, and max_align_t is 16 on x86-64 / arm64. */
#define MM_ALIGN ((size_t)16)

#define MM_HEAP_MAGIC UINT64_C(0x4D794D616C6C6F63) /* "MyMalloc" */
#define MM_BLOCK_MAGIC ((uint8_t)0xDD)

typedef struct mm_block {
    uint8_t magic;        /* MM_BLOCK_MAGIC, used to validate the walk */
    bool in_use;
    uint16_t padding0;    /* explicit padding: sizeof(mm_block) == 32 */
    uint32_t padding1;
    size_t length;        /* payload bytes, header excluded */
    struct mm_block *prev;
    struct mm_block *next;
} mm_block;

typedef struct {
    uint64_t magic;
    size_t block_count;
    size_t page_count;
    size_t heap_size;     /* bytes committed so far */
} mm_heap;

/* Local equivalent of max_align_t: MSVC's C mode does not define max_align_t,
 * even with /std:c11. The union has the alignment of its strictest member. */
typedef union {
    long double ld;
    long long ll;
    double d;
    void *p;
    void (*fn)(void);
} mm_max_alignment;

_Static_assert(sizeof(mm_block) == 32, "block header must be 32 bytes");
_Static_assert(sizeof(mm_heap) == 32, "heap header must be 32 bytes");
_Static_assert(_Alignof(mm_max_alignment) <= MM_ALIGN,
               "MM_ALIGN must satisfy the strictest fundamental alignment");

/* ------------------------------------------------------------------ */
/* state                                                              */
/* ------------------------------------------------------------------ */

static mm_heap *g_heap;
static atomic_int g_init_state; /* 0 = idle, 1 = initialising, 2 = ready */
static atomic_flag g_lock = ATOMIC_FLAG_INIT;

static void mm_lock(void)
{
    while (atomic_flag_test_and_set_explicit(&g_lock, memory_order_acquire)) {
        mm_os_yield();
    }
}

static void mm_unlock(void)
{
    atomic_flag_clear_explicit(&g_lock, memory_order_release);
}

/* ------------------------------------------------------------------ */
/* small helpers                                                      */
/* ------------------------------------------------------------------ */

static mm_block *mm_first_block(void)
{
    return (mm_block *)((char *)g_heap + sizeof(mm_heap));
}

static mm_block *mm_last_block(void)
{
    mm_block *b = mm_first_block();
    while (b->next != NULL) {
        b = b->next;
    }
    return b;
}

/* Round up to MM_ALIGN. Returns 0 on overflow. */
static size_t mm_align_up(size_t n)
{
    if (n > SIZE_MAX - (MM_ALIGN - 1)) {
        return 0;
    }
    return (n + (MM_ALIGN - 1)) & ~(MM_ALIGN - 1);
}

static bool mm_ptr_in_heap(const void *ptr)
{
    const char *p = (const char *)ptr;
    return p >= (const char *)g_heap + sizeof(mm_heap) &&
           p < (const char *)g_heap + g_heap->heap_size;
}

/* ------------------------------------------------------------------ */
/* initialisation                                                     */
/* ------------------------------------------------------------------ */

static void mm_do_init(void)
{
    void *base = mm_os_reserve(MM_HEAP_RESERVE);
    if (base == NULL) {
        return; /* g_heap stays NULL: every allocation will fail cleanly */
    }

    g_heap = (mm_heap *)base;
    if (!mm_os_commit(base, 0, MM_PAGE_SIZE)) {
        mm_os_release(base, MM_HEAP_RESERVE);
        g_heap = NULL;
        return;
    }

    g_heap->magic = MM_HEAP_MAGIC;
    g_heap->block_count = 1;
    g_heap->page_count = 1;
    g_heap->heap_size = MM_PAGE_SIZE;

    mm_block *first = mm_first_block();
    first->magic = MM_BLOCK_MAGIC;
    first->in_use = false;
    first->padding0 = 0;
    first->padding1 = 0;
    first->length = MM_PAGE_SIZE - sizeof(mm_heap) - sizeof(mm_block);
    first->prev = NULL;
    first->next = NULL;
}

void mm_init(void)
{
    int expected = 0;
    if (atomic_compare_exchange_strong_explicit(&g_init_state, &expected, 1,
                                                memory_order_acq_rel,
                                                memory_order_acquire)) {
        mm_do_init();
        atomic_store_explicit(&g_init_state, 2, memory_order_release);
        return;
    }
    /* Another thread is initialising: wait for it. This is the race the
     * article's reader reported, fixed without a second heap field. */
    while (atomic_load_explicit(&g_init_state, memory_order_acquire) != 2) {
        mm_os_yield();
    }
}

/* ------------------------------------------------------------------ */
/* growing                                                            */
/* ------------------------------------------------------------------ */

/* Commit `bytes` at the end of the heap and extend the list.
 * `bytes` must be a non-zero multiple of the page size. */
static bool mm_extend(size_t bytes)
{
    if (bytes < MM_PAGE_SIZE || bytes % MM_PAGE_SIZE != 0) {
        return false;
    }
    if (!mm_os_commit(g_heap, g_heap->heap_size, bytes)) {
        return false;
    }
    g_heap->heap_size += bytes;
    g_heap->page_count += bytes / MM_PAGE_SIZE;

    mm_block *last = mm_last_block();
    if (!last->in_use) {
        /* The list already ends with a free block: just make it bigger. */
        last->length += bytes;
        return true;
    }

    /* The tail is in use, so we need a brand new free block after it. */
    mm_block *fresh = (mm_block *)((char *)last + sizeof(mm_block) + last->length);
    fresh->magic = MM_BLOCK_MAGIC;
    fresh->in_use = false;
    fresh->padding0 = 0;
    fresh->padding1 = 0;
    fresh->length = bytes - sizeof(mm_block);
    fresh->prev = last;
    fresh->next = NULL;
    last->next = fresh;
    g_heap->block_count++;
    return true;
}

static mm_block *mm_best_fit(size_t size)
{
    mm_block *best = NULL;
    for (mm_block *b = mm_first_block(); b != NULL; b = b->next) {
        if (!b->in_use && b->length >= size &&
            (best == NULL || b->length < best->length)) {
            best = b;
        }
    }
    return best;
}

static size_t mm_largest_free(void)
{
    size_t largest = 0;
    for (mm_block *b = mm_first_block(); b != NULL; b = b->next) {
        if (!b->in_use && b->length > largest) {
            largest = b->length;
        }
    }
    return largest;
}

/* ------------------------------------------------------------------ */
/* shrinking                                                          */
/* ------------------------------------------------------------------ */

/* Give back whole pages that live past the end of the last block.
 *
 * If the tail block is free it can also be truncated: we only keep the
 * smallest number of whole pages able to host its header plus one aligned
 * payload. Without that step a free tail block spanning the whole heap would
 * never release anything. */
static void mm_shrink_tail(void)
{
    mm_block *last = mm_last_block();
    size_t last_offset = (size_t)((char *)last - (char *)g_heap);
    size_t last_end = last_offset + sizeof(mm_block) + last->length;

    if (!last->in_use) {
        size_t minimum = last_offset + sizeof(mm_block) + MM_ALIGN;
        size_t keep = ((minimum + MM_PAGE_SIZE - 1) / MM_PAGE_SIZE) * MM_PAGE_SIZE;
        if (keep > last_end) {
            keep = last_end;
        }
        last->length = keep - last_offset - sizeof(mm_block);
        last_end = keep;
    }

    while (g_heap->heap_size >= last_end + MM_PAGE_SIZE) {
        mm_os_decommit(g_heap, g_heap->heap_size - MM_PAGE_SIZE, MM_PAGE_SIZE);
        g_heap->heap_size -= MM_PAGE_SIZE;
        g_heap->page_count--;
    }
}

/* ------------------------------------------------------------------ */
/* public API                                                         */
/* ------------------------------------------------------------------ */

void *my_malloc(size_t size)
{
    mm_init();
    if (g_heap == NULL || size == 0) {
        return NULL;
    }

    size_t need = mm_align_up(size);
    if (need == 0) {
        return NULL; /* overflow */
    }

    mm_lock();

    mm_block *block = mm_best_fit(need);
    for (int guard = 0; block == NULL; guard++) {
        if (guard > 4096) {
            mm_unlock();
            return NULL;
        }
        size_t largest = mm_largest_free();
        size_t missing = need > largest ? need - largest : 0;
        if (missing > SIZE_MAX - sizeof(mm_block) - (MM_PAGE_SIZE - 1)) {
            mm_unlock();
            return NULL; /* the request cannot be represented */
        }
        size_t pages =
            (missing + sizeof(mm_block) + MM_PAGE_SIZE - 1) / MM_PAGE_SIZE;
        if (pages == 0) {
            pages = 1;
        }
        if (pages > SIZE_MAX / MM_PAGE_SIZE ||
            !mm_extend(pages * MM_PAGE_SIZE)) {
            mm_unlock();
            return NULL;
        }
        block = mm_best_fit(need);
    }

    /* Split, unless the leftover is too small to hold a header. */
    size_t remainder = block->length - need;
    if (remainder >= sizeof(mm_block) + MM_ALIGN) {
        mm_block *rest = (mm_block *)((char *)block + sizeof(mm_block) + need);
        rest->magic = MM_BLOCK_MAGIC;
        rest->in_use = false;
        rest->padding0 = 0;
        rest->padding1 = 0;
        rest->length = remainder - sizeof(mm_block);
        rest->prev = block;
        rest->next = block->next;
        if (rest->next != NULL) {
            rest->next->prev = rest;
        }
        block->next = rest;
        block->length = need;
        g_heap->block_count++;
    }

    block->in_use = true;
    void *payload = (char *)block + sizeof(mm_block);
    mm_unlock();
    return payload;
}

bool my_free(void *ptr)
{
    if (ptr == NULL) {
        return true;
    }
    mm_init();
    if (g_heap == NULL || !mm_ptr_in_heap(ptr)) {
        return false;
    }

    mm_lock();

    mm_block *block = (mm_block *)((char *)ptr - sizeof(mm_block));
    if ((char *)block < (char *)g_heap + sizeof(mm_heap) ||
        block->magic != MM_BLOCK_MAGIC || !block->in_use) {
        mm_unlock();
        return false; /* foreign pointer or double free */
    }

    block->in_use = false;

    /* Merge with the next block. */
    if (block->next != NULL && !block->next->in_use) {
        mm_block *next = block->next;
        block->next = next->next;
        if (next->next != NULL) {
            next->next->prev = block;
        }
        block->length += sizeof(mm_block) + next->length;
        next->magic = 0; /* poison, so a stale pointer is rejected later */
        g_heap->block_count--;
    }

    /* Merge with the previous block. */
    if (block->prev != NULL && !block->prev->in_use) {
        mm_block *prev = block->prev;
        prev->next = block->next;
        if (block->next != NULL) {
            block->next->prev = prev;
        }
        prev->length += sizeof(mm_block) + block->length;
        block->magic = 0;
        g_heap->block_count--;
    }

    mm_shrink_tail();
    mm_unlock();
    return true;
}

size_t my_usable_size(void *ptr)
{
    if (ptr == NULL || g_heap == NULL || !mm_ptr_in_heap(ptr)) {
        return 0;
    }
    mm_block *block = (mm_block *)((char *)ptr - sizeof(mm_block));
    if (block->magic != MM_BLOCK_MAGIC || !block->in_use) {
        return 0;
    }
    return block->length;
}

void mm_get_stats(mm_stats *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    mm_init();
    if (g_heap == NULL) {
        return;
    }

    mm_lock();
    out->block_count = g_heap->block_count;
    out->page_count = g_heap->page_count;
    out->heap_size = g_heap->heap_size;
    for (mm_block *b = mm_first_block(); b != NULL; b = b->next) {
        if (b->in_use) {
            out->allocated_bytes += b->length;
        } else {
            out->free_bytes += b->length;
            if (b->length > out->largest_free) {
                out->largest_free = b->length;
            }
        }
    }
    mm_unlock();
}

bool mm_check(void)
{
    if (g_heap == NULL || g_heap->magic != MM_HEAP_MAGIC) {
        return false;
    }

    const char *heap_lo = (const char *)g_heap + sizeof(mm_heap);
    const char *heap_hi = (const char *)g_heap + g_heap->heap_size;
    const char *expected = heap_lo;
    mm_block *prev = NULL;
    size_t count = 0;

    for (mm_block *b = mm_first_block(); b != NULL; b = b->next) {
        if ((const char *)b != expected) {
            return false; /* gap or overlap in the list */
        }
        if (b->magic != MM_BLOCK_MAGIC || b->prev != prev) {
            return false;
        }
        if ((b->length % MM_ALIGN) != 0) {
            return false; /* payloads must keep their alignment */
        }
        if (!b->in_use && prev != NULL && !prev->in_use) {
            return false; /* two free blocks should have been merged */
        }
        if ((const char *)b + sizeof(mm_block) + b->length > heap_hi) {
            return false; /* block runs past the committed heap */
        }
        expected = (const char *)b + sizeof(mm_block) + b->length;
        prev = b;
        count++;
    }

    /* The list must cover the committed heap exactly: no trailing page. */
    return count == g_heap->block_count && count > 0 && expected == heap_hi;
}

void mm_dump(FILE *out)
{
    if (out == NULL || g_heap == NULL) {
        return;
    }

    fprintf(out, "  %-4s %-10s %-8s %-8s\n", "#", "offset", "state", "bytes");
    size_t index = 0;
    for (mm_block *b = mm_first_block(); b != NULL; b = b->next, index++) {
        size_t offset = (size_t)((char *)b - (char *)g_heap);
        fprintf(out, "  %-4zu %-10zu %-8s %-8zu\n", index, offset,
                b->in_use ? "used" : "free", b->length);
    }
}
