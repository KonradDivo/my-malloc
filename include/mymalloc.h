/*
 * mymalloc.h - a small, educational heap allocator.
 *
 * Reimplementation of the allocator described in
 * "Malloc is not magic - Implementing my own memory allocator"
 * by Alejandro Nadal (Level Up Coding, April 2025):
 * https://levelup.gitconnected.com/malloc-is-not-magic-implementing-my-own-memory-allocator-e0354e914402
 *
 * This is a teaching project, NOT a replacement for the C library malloc.
 */
#ifndef MYMALLOC_H
#define MYMALLOC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    size_t block_count;     /* blocks in the linked list */
    size_t page_count;      /* pages committed from the OS */
    size_t heap_size;       /* bytes committed from the OS */
    size_t allocated_bytes; /* sum of in-use payloads */
    size_t free_bytes;      /* sum of free payloads */
    size_t largest_free;    /* biggest free payload */
} mm_stats;

/* Initialise the heap. Called automatically by my_malloc(); calling it
 * explicitly is only useful to make the first allocation predictable. */
void mm_init(void);

/* Allocate at least `size` bytes. Returns NULL on failure or when size == 0. */
void *my_malloc(size_t size);

/* Release a block. Returns false if `ptr` is not a live allocation of ours
 * (invalid pointer, double free). Passing NULL is a successful no-op. */
bool my_free(void *ptr);

/* Usable payload size of a live allocation, 0 if the pointer is invalid. */
size_t my_usable_size(void *ptr);

/* Walk the heap and fill `out`. */
void mm_get_stats(mm_stats *out);

/* Verify the invariants of the block list. Returns false if anything is off. */
bool mm_check(void);

/* Print the block list as a table. */
void mm_dump(FILE *out);

#endif /* MYMALLOC_H */
