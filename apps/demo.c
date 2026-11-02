/*
 * demo.c - walk through the allocator's behaviour, one step at a time.
 *
 * Build and run:
 *     cmake -S . -B build && cmake --build build && ./build/demo
 */
#include "mymalloc.h"

#include <stdio.h>
#include <string.h>

static void show(const char *title)
{
    mm_stats s;
    mm_get_stats(&s);
    printf("\n[%s]\n", title);
    printf("  blocks=%zu  pages=%zu  heap=%zu B  used=%zu B  free=%zu B"
           "  largest_free=%zu B\n",
           s.block_count, s.page_count, s.heap_size, s.allocated_bytes,
           s.free_bytes, s.largest_free);
}

int main(void)
{
    mm_init();
    printf("=== my malloc, after Alejandro Nadal's article ===\n");
    show("initial state");
    mm_dump(stdout);

    char *a = my_malloc(64);
    char *b = my_malloc(128);
    char *c = my_malloc(256);
    if (a == NULL || b == NULL || c == NULL) {
        printf("allocation failed\n");
        return 1;
    }
    strcpy(a, "alpha");
    strcpy(b, "bravo");
    strcpy(c, "charlie");

    show("3 allocations: 64 / 128 / 256 bytes");
    mm_dump(stdout);

    printf("\nfree(b): the middle block becomes free\n");
    my_free(b);
    show("after free(b)");
    mm_dump(stdout);

    printf("\nfree(a) then free(c): all three blocks merge\n");
    my_free(a);
    my_free(c);
    show("after free(a) and free(c)");
    mm_dump(stdout);

    printf("\nmy_malloc(5000): larger than one page, the heap must grow\n");
    unsigned char *big = my_malloc(5000);
    if (big == NULL) {
        printf("allocation failed\n");
        return 1;
    }
    memset(big, 0xAB, 5000);
    show("after my_malloc(5000)");
    mm_dump(stdout);

    printf("\nfree(big): the extra pages are returned to the OS\n");
    my_free(big);
    show("after free(big)");
    mm_dump(stdout);

    printf("\n--- the traps ---\n");
    char *x = my_malloc(32);
    printf("free(x)        : %s\n",
           my_free(x) ? "accepted (normal)" : "rejected (abnormal)");
    printf("double free    : %s\n",
           my_free(x) ? "ACCEPTED (abnormal)" : "correctly rejected");

    int local = 0;
    printf("invalid free   : %s\n",
           my_free(&local) ? "ACCEPTED (abnormal)" : "correctly rejected");
    printf("free(NULL)     : %s\n",
           my_free(NULL) ? "accepted (normal)" : "rejected (abnormal)");

    printf("\nheap integrity : %s\n", mm_check() ? "OK" : "CORRUPT");
    return 0;
}
