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
    printf("  blocs=%zu  pages=%zu  tas=%zu o  alloue=%zu o  libre=%zu o"
           "  plus_grand_libre=%zu o\n",
           s.block_count, s.page_count, s.heap_size, s.allocated_bytes,
           s.free_bytes, s.largest_free);
}

int main(void)
{
    mm_init();
    printf("=== mon malloc, d'apres l'article d'Alejandro Nadal ===\n");
    show("etat initial");
    mm_dump(stdout);

    char *a = my_malloc(64);
    char *b = my_malloc(128);
    char *c = my_malloc(256);
    if (a == NULL || b == NULL || c == NULL) {
        printf("allocation impossible\n");
        return 1;
    }
    strcpy(a, "alpha");
    strcpy(b, "bravo");
    strcpy(c, "charlie");

    show("3 allocations : 64 / 128 / 256 octets");
    mm_dump(stdout);

    printf("\nfree(b) : le bloc du milieu devient libre\n");
    my_free(b);
    show("apres free(b)");
    mm_dump(stdout);

    printf("\nfree(a) puis free(c) : les trois blocs fusionnent\n");
    my_free(a);
    my_free(c);
    show("apres free(a) et free(c)");
    mm_dump(stdout);

    printf("\nmy_malloc(5000) : plus grand qu'une page, le tas doit grandir\n");
    unsigned char *big = my_malloc(5000);
    if (big == NULL) {
        printf("allocation impossible\n");
        return 1;
    }
    memset(big, 0xAB, 5000);
    show("apres my_malloc(5000)");
    mm_dump(stdout);

    printf("\nfree(big) : les pages en trop sont rendues a l'OS\n");
    my_free(big);
    show("apres free(big)");
    mm_dump(stdout);

    printf("\n--- les pieges ---\n");
    char *x = my_malloc(32);
    printf("free(x)        : %s\n",
           my_free(x) ? "accepte (normal)" : "refuse (anormal)");
    printf("double free    : %s\n",
           my_free(x) ? "ACCEPTE (anormal)" : "refuse correctement");

    int local = 0;
    printf("free invalide  : %s\n",
           my_free(&local) ? "ACCEPTE (anormal)" : "refuse correctement");
    printf("free(NULL)     : %s\n",
           my_free(NULL) ? "accepte (normal)" : "refuse (anormal)");

    printf("\nintegrite du tas : %s\n", mm_check() ? "OK" : "CORROMPU");
    return 0;
}
