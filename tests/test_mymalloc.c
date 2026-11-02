/*
 * test_mymalloc.c - the article's testing idea, made portable.
 *
 * Each test runs in its own process, so a crash or an assert only kills that
 * one test instead of the whole run:
 *   - POSIX  : fork() in the parent, the child runs the single test.
 *   - Windows: the executable re-executes itself with the test name.
 *
 * Run every test:      ./build/test_mymalloc
 * Run a single test:   ./build/test_mymalloc stress
 */
#include "mymalloc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <process.h>
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "  ECHEC %s:%d  (%s)\n", __FILE__, __LINE__, \
                    #cond);                                             \
            fflush(stderr);                                             \
            abort();                                                    \
        }                                                               \
    } while (0)

/* ------------------------------------------------------------------ */
/* test cases                                                         */
/* ------------------------------------------------------------------ */

static void test_init(void)
{
    mm_init();
    mm_stats s;
    mm_get_stats(&s);
    CHECK(s.block_count == 1);
    CHECK(s.page_count == 1);
    CHECK(s.heap_size == 4096);
    CHECK(s.allocated_bytes == 0);
    CHECK(s.free_bytes > 0);
    CHECK(mm_check());
}

static void test_basic(void)
{
    char *p = my_malloc(64);
    CHECK(p != NULL);
    memset(p, 0x5A, 64);
    for (size_t i = 0; i < 64; i++) {
        CHECK((unsigned char)p[i] == 0x5A);
    }
    CHECK(my_usable_size(p) >= 64);
    CHECK(mm_check());
}

static void test_alignment(void)
{
    for (size_t n = 1; n < 400; n += 7) {
        void *p = my_malloc(n);
        CHECK(p != NULL);
        /* malloc() must return memory usable for any type. */
        CHECK(((uintptr_t)p % 16u) == 0);
    }
    CHECK(mm_check());
}

static void test_grow(void)
{
    unsigned char *p = my_malloc(5000);
    CHECK(p != NULL);
    for (size_t i = 0; i < 5000; i++) {
        p[i] = (unsigned char)(i * 31u);
    }
    for (size_t i = 0; i < 5000; i++) {
        CHECK(p[i] == (unsigned char)(i * 31u));
    }
    mm_stats s;
    mm_get_stats(&s);
    CHECK(s.page_count >= 2);
    CHECK(mm_check());
}

static void test_coalesce(void)
{
    void *a = my_malloc(64);
    void *b = my_malloc(64);
    void *c = my_malloc(64);
    CHECK(a != NULL && b != NULL && c != NULL);

    mm_stats s0;
    mm_get_stats(&s0);
    CHECK(s0.block_count == 4); /* 3 used + the free tail */

    CHECK(my_free(b));
    CHECK(my_free(a));
    CHECK(my_free(c));

    mm_stats s1;
    mm_get_stats(&s1);
    CHECK(s1.block_count == 1); /* everything merged back together */
    CHECK(s1.allocated_bytes == 0);
    CHECK(mm_check());
}

static void test_best_fit(void)
{
    char *x = my_malloc(64);
    char *hole = my_malloc(1000);
    char *z = my_malloc(64);
    CHECK(x != NULL && hole != NULL && z != NULL);

    CHECK(my_free(hole));

    /* Ask for something small: best fit must pick the 1000-byte hole rather
     * than the much bigger free tail. */
    char *reused = my_malloc(48);
    CHECK(reused != NULL);
    uintptr_t reused_addr = (uintptr_t)reused;
    uintptr_t hole_addr = (uintptr_t)hole;
    CHECK(reused_addr >= hole_addr && reused_addr < hole_addr + 1000);
    CHECK(mm_check());
}

static void test_invalid_free(void)
{
    void *p = my_malloc(32);
    CHECK(p != NULL);
    CHECK(!my_free(&p)); /* address on the stack */

    int local = 0;
    CHECK(!my_free(&local));
    CHECK(my_free(NULL));
}

static void test_double_free(void)
{
    void *p = my_malloc(32);
    CHECK(p != NULL);
    CHECK(my_free(p));
    CHECK(!my_free(p));
}

static uint64_t g_rng = UINT64_C(0x123456789ABCDEF);

static uint64_t rng_next(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

static void test_stress(void)
{
    enum { SLOTS = 256, ITERATIONS = 20000 };
    struct slot {
        unsigned char *p;
        size_t n;
        unsigned char tag;
    } slots[SLOTS];

    memset(slots, 0, sizeof(slots));

    for (int iter = 0; iter < ITERATIONS; iter++) {
        size_t i = (size_t)(rng_next() % SLOTS);
        if (slots[i].p == NULL) {
            size_t n = (size_t)(rng_next() % 512) + 1;
            unsigned char tag = (unsigned char)(rng_next() % 255 + 1);
            slots[i].p = my_malloc(n);
            CHECK(slots[i].p != NULL);
            slots[i].n = n;
            slots[i].tag = tag;
            memset(slots[i].p, tag, n);
        } else {
            for (size_t k = 0; k < slots[i].n; k++) {
                CHECK(slots[i].p[k] == slots[i].tag);
            }
            CHECK(my_free(slots[i].p));
            slots[i].p = NULL;
        }
        if ((iter % 500) == 0) {
            CHECK(mm_check());
        }
    }

    for (size_t i = 0; i < SLOTS; i++) {
        if (slots[i].p != NULL) {
            CHECK(my_free(slots[i].p));
        }
    }
    CHECK(mm_check());

    mm_stats s;
    mm_get_stats(&s);
    CHECK(s.block_count == 1);
    CHECK(s.allocated_bytes == 0);
}

/* ------------------------------------------------------------------ */
/* runner                                                             */
/* ------------------------------------------------------------------ */

static const struct {
    const char *name;
    void (*fn)(void);
} TESTS[] = {
    {"init", test_init},           {"basic", test_basic},
    {"alignment", test_alignment}, {"grow", test_grow},
    {"coalesce", test_coalesce},   {"best_fit", test_best_fit},
    {"invalid_free", test_invalid_free},
    {"double_free", test_double_free},
    {"stress", test_stress},
};

#define NTESTS (sizeof(TESTS) / sizeof(TESTS[0]))

static int run_named(const char *name)
{
    for (size_t i = 0; i < NTESTS; i++) {
        if (strcmp(TESTS[i].name, name) == 0) {
            TESTS[i].fn();
            return 0;
        }
    }
    fprintf(stderr, "test inconnu : %s\n", name);
    return 2;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        return run_named(argv[1]);
    }

    size_t failed = 0;

    for (size_t i = 0; i < NTESTS; i++) {
        bool ok = false;

#if defined(_WIN32)
        char exe[MAX_PATH];
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        const char *args[3] = {exe, TESTS[i].name, NULL};
        intptr_t status = _spawnv(_P_WAIT, exe, args);
        ok = (status == 0);
        if (!ok) {
            printf("[FAIL] %-14s code=%d\n", TESTS[i].name, (int)status);
        }
#else
        pid_t pid = fork();
        if (pid == 0) {
            TESTS[i].fn();
            _exit(0);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFSIGNALED(status)) {
            printf("[FAIL] %-14s signal %d\n", TESTS[i].name,
                   WTERMSIG(status));
        } else {
            ok = (WEXITSTATUS(status) == 0);
            if (!ok) {
                printf("[FAIL] %-14s code=%d\n", TESTS[i].name,
                       WEXITSTATUS(status));
            }
        }
#endif

        if (ok) {
            printf("[ ok ] %s\n", TESTS[i].name);
        } else {
            failed++;
        }
    }

    printf("\n%zu/%zu tests passes\n", NTESTS - failed, (size_t)NTESTS);
    return failed == 0 ? 0 : 1;
}
