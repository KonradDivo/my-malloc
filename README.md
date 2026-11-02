# my-malloc

**A small `malloc` / `free` implementation you can actually read.**

This project implements a dynamic memory allocator from scratch in C11. It is a
reimplementation of the allocator described in Alejandro Nadal's article
[*Malloc is not magic — Implementing my own memory allocator*](https://levelup.gitconnected.com/malloc-is-not-magic-implementing-my-own-memory-allocator-e0354e914402).
The goal is simple: make the machinery behind `malloc` visible.

It runs on Linux, WSL, macOS and Windows, and needs nothing but a C11 compiler and CMake.

> **This is teaching software, not production software.** It has no `realloc` or
> `calloc`, no performance tuning, and no security hardening. Do not use it as a
> replacement for your C library, and do not load it with `LD_PRELOAD`.

---

## Who is this for?

- Students learning C and memory management.
- Programmers who want to know what `malloc` really does.
- Anyone who prefers a small, complete example to a wall of theory.

You only need basic C. Allocator-specific terms are explained in the
[glossary](#glossary).

---

## What it is, and what it is not

| It is | It is not |
|---|---|
| A readable allocator (~500 lines of C) | A drop-in replacement for `malloc` |
| A lab for experiments | A fast, production-grade allocator |
| Portable across Linux/macOS/Windows | Hardened against attackers |
| Tested (9 tests + a randomized stress test) | A reason to skip the C library |

---

## Try it in two minutes

### Linux, WSL or macOS

```bash
cmake -S . -B build
cmake --build build
./build/demo
ctest --test-dir build --output-on-failure
```

### Windows with MSYS2 (UCRT64)

In an **MSYS2 UCRT64** terminal:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc \
                   mingw-w64-ucrt-x86_64-cmake \
                   mingw-w64-ucrt-x86_64-ninja
cmake -S . -B build -G Ninja
cmake --build build
./build/demo.exe
./build/test_mymalloc.exe
```

If you run CMake from PowerShell instead, put the compiler on `PATH` first:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER="C:/msys64/ucrt64/bin/gcc.exe"
cmake --build build
```

<details>
<summary>Windows with Visual Studio 2022</summary>

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`CMakeLists.txt` adds `/std:c11 /experimental:c11atomics` for MSVC. Without the
second flag, MSVC refuses `<stdatomic.h>`.
</details>

### Options

| Option | Default | Meaning |
|---|---|---|
| `MYMALLOC_BUILD_TESTS` | `ON` | Build the test suite and register it with CTest. |
| `MYMALLOC_USE_SBRK` | `OFF` | Use the article's `sbrk()` backend. **POSIX only.** |

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake -S . -B build-sbrk -DMYMALLOC_USE_SBRK=ON   # Linux, WSL, macOS
```

---

## What the demo shows

`demo` allocates three blocks, frees them in a different order, asks for a block
larger than one page, then frees it. A shortened run:

```text
[3 allocations: 64 / 128 / 256 bytes]
  blocks=4  pages=1  heap=4096 B  used=448 B  free=3488 B  largest_free=3488 B
  #    offset     state    bytes
  0    32         used     64
  1    128        used     128
  2    288        used     256
  3    576        free     3488

[after free(a) and free(c)]
  blocks=1  pages=1  heap=4096 B  used=0 B  free=4032 B  largest_free=4032 B
  #    offset     state    bytes
  0    32         free     4032

[after free(big)]
  blocks=1  pages=1  heap=4096 B  used=0 B  free=4032 B  largest_free=4032 B
  #    offset     state    bytes
  0    32         free     4032

--- the traps ---
free(x)        : accepted (normal)
double free    : correctly rejected
invalid free   : correctly rejected
free(NULL)     : accepted (normal)

heap integrity : OK
```

Read it like this:

1. Three allocations split one free block into four blocks.
2. Freeing the middle block creates a hole but does not merge anything yet.
3. Freeing the two neighbours makes all three blocks merge back into one.
4. A 5000-byte request grows the heap to two pages; freeing it gives the second
   page back to the operating system.

---

## How it works, without the jargon

### A warehouse full of boxes

Imagine the heap as a warehouse. The operating system gives the warehouse
fixed-size rooms called **pages** (4 KiB each). Inside those rooms, the
allocator stores **blocks**, like boxes on a shelf.

Every block starts with a **header**: a small label that records the block's
size, whether it is in use, and where the previous and next blocks are. The
bytes after the header are the **payload** — the part returned to your program.

```text
heap start
+---------------------------+
| heap header (32 bytes)    |  bookkeeping for the whole heap
+---------------------------+
| block header (32 bytes)   |
| payload                   |  <- pointer returned to the caller
+---------------------------+
| block header (32 bytes)   |
| payload                   |
+---------------------------+
| ...                       |
+---------------------------+  <- end of committed pages
```

Blocks are placed back to back. That is the key invariant: the next block
always starts exactly where the previous payload ends. No gaps, no overlaps.

### `my_malloc(size)` in four steps

1. Round `size` up to a multiple of 16, so the payload is correctly aligned.
2. Walk the list and pick the **smallest free block that is big enough**
   (this strategy is called *best fit*).
3. If no block fits, ask the OS for more pages and add them to the end.
4. If the chosen block is much larger than needed, **split** it: keep the first
   part for the caller and leave the rest as a new free block.

Then mark the block as used and return a pointer to its payload.

### `my_free(ptr)` in four steps

1. Check that `ptr` looks like one of our allocations (inside the heap, valid
   header, marked as used). If not, refuse and return `false`.
2. Mark the block as free.
3. **Merge** it with its free neighbours, so small holes become one big hole.
4. If the free block is at the end of the heap, give whole unused pages back to
   the OS.

### The rules the heap must follow

`mm_check()` verifies these rules:

- the linked list covers the heap without gaps or overlaps;
- `prev` and `next` agree with each other;
- every header has the expected magic byte;
- every payload size is a multiple of 16;
- two free blocks are never adjacent (they should have been merged);
- the last block ends exactly at the end of the committed heap.

If any rule breaks, `mm_check()` returns `false`. The tests call it regularly,
especially during the randomized stress test.

### Where the memory comes from

The allocator never calls the OS directly. It goes through a small interface in
`src/mm_os.c`, so the same allocator works on several platforms.

| Platform | Reserve address space | Make pages usable | Give pages back |
|---|---|---|---|
| Windows | `VirtualAlloc(MEM_RESERVE)` | `VirtualAlloc(MEM_COMMIT)` | `VirtualFree(MEM_DECOMMIT)` |
| Linux/macOS (default) | `mmap` | first write to the page | `madvise(MADV_DONTNEED)` |
| Linux/macOS with `-DMYMALLOC_USE_SBRK=ON` | `sbrk(0)` | `sbrk(+n)` | `sbrk(-n)` |

**Why `mmap` is the default on POSIX.** The C library also uses `sbrk()` for its
own heap. If we move the same "program break" behind its back, `printf` or
`fopen` can end up writing into memory we think we own. `mmap` gives us a
private arena that the C library never touches. The `sbrk` mode is kept because
it is the article's approach, and because watching that conflict happen is
instructive. It is not safe when the C library allocates between our calls.

### Threads and alignment

- A global spinlock protects `my_malloc` and `my_free`.
- Initialization uses a C11 atomic compare-and-swap, so two threads cannot
  create two heaps by accident.
- `my_usable_size`, `mm_dump` and `mm_check` read without the lock. Call them
  only when the heap is quiet.
- Every payload is aligned on 16 bytes, which is enough for any standard C type
  on x86-64 and arm64.

---

## Glossary

| Term | Plain-English meaning |
|---|---|
| **Allocator** | Code that hands out memory and recycles it later. |
| **Heap** | The pool of memory the allocator manages. |
| **Page** | A fixed-size chunk obtained from the OS (4 KiB here). |
| **Block** | A header followed by a payload. |
| **Header / metadata** | Bookkeeping bytes stored just before the payload. |
| **Payload** | The bytes your program actually uses. |
| **Free list** | The blocks that are not in use. Here they live in the same linked list as used blocks, with a flag. |
| **Best fit** | Pick the smallest free block that is still big enough. |
| **Split** | Cut the unused tail off a block and turn it into a new free block. |
| **Coalescing / merge** | Join adjacent free blocks into one larger block. |
| **Fragmentation** | Free memory exists, but in pieces too small to reuse. |
| **Alignment** | An address divisible by a given number (16 here). |
| **Commit / decommit** | Make pages usable / give them back to the OS. |
| **`mmap`, `sbrk`, `VirtualAlloc`** | OS calls used to reserve and manage memory. |
| **Atomic** | An operation that cannot be interrupted halfway by another thread. |
| **Spinlock** | A lock that waits in a loop until it becomes free. |
| **Magic number** | A known marker used to sanity-check a header. |
| **Undefined behavior** | The C standard makes no promise: the program may crash or corrupt memory. |
| **Hardening** | Extra defenses that make memory bugs harder to exploit. See below. |

---

## What "hardening" means

You will see the word *hardening* in security discussions. It means: **adding
defenses so that a memory bug is less likely to crash the program or become an
exploit.** A normal allocator trusts its own bookkeeping. A hardened one checks
it and tries to fail safely.

Typical allocator hardening techniques:

- **Guard pages** — unmapped pages around an allocation, so a buffer overflow
  hits a wall instead of another object.
- **Canaries** — known values stored next to metadata and checked later, to
  detect overwrites.
- **Randomized layout** — makes addresses harder to predict.
- **Read-only metadata** — makes it harder to corrupt the allocator's own data.
- **Strict pointer validation** — reject pointers that do not belong to a live
  allocation.

What this project does:

- checks that the pointer is inside the heap;
- checks a magic byte in the header;
- checks the `in_use` flag;
- rejects double frees and foreign pointers;
- verifies the heap invariants with `mm_check()`.

What this project does **not** do:

- no guard pages, canaries, address randomization or read-only metadata;
- a forged pointer that happens to look valid can still get through;
- the checks are debugging aids, not a security boundary.

So when this README says "not hardened", it means: **use it to learn, not to
protect secrets or run untrusted code.**

---

## Tests

`tests/test_mymalloc.c` contains nine tests. Each one runs in its own process:
`fork()` on POSIX, or a re-exec of the same executable on Windows. A crash or a
failed assertion kills only that test, and the run continues.

| Test | What it checks |
|---|---|
| `init` | The fresh heap has one free block, one page, and valid invariants. |
| `basic` | Allocation, write/read, and usable size. |
| `alignment` | Every returned pointer is 16-byte aligned. |
| `grow` | A 5000-byte request grows the heap and preserves the data. |
| `coalesce` | Three blocks freed out of order merge back into one. |
| `best_fit` | A small request reuses the 1000-byte hole, not the larger tail. |
| `invalid_free` | A stack pointer is rejected; `free(NULL)` is accepted. |
| `double_free` | The second free of the same pointer is rejected. |
| `stress` | 20,000 random allocations/frees with content and invariant checks. |

Run everything:

```bash
ctest --test-dir build --output-on-failure
# or
./build/test_mymalloc
```

Run one test:

```bash
./build/test_mymalloc stress
```

A GitHub Actions workflow (`.github/workflows/ci.yml`) builds and tests on
Linux and Windows/MSYS2, and checks that the `sbrk` backend still compiles on
Linux.

---

## How this differs from the article

The core algorithm is the same: one doubly linked list, best fit, split on
allocation, merge on free. This version adds the parts needed to make that idea
portable and less fragile:

- an OS abstraction layer, with `mmap`/`VirtualAlloc`/`sbrk` backends;
- 16-byte alignment for every payload;
- a minimum split size, so we never create an unusable crumb;
- rejection of double frees and foreign pointers;
- atomic initialization (no race between threads);
- returning unused tail pages to the OS;
- `mm_stats`, `mm_dump` and `mm_check` for introspection;
- a portable test runner and a CI workflow.

The article is a teaching article, not a production allocator. These additions
are not a criticism: they are the extra steps you need once the example leaves
the page.

---

## Limits

- No `calloc`, `realloc`, `aligned_alloc` or `memalign`.
- Allocation is a linear search: cost grows with the number of blocks.
- One global lock, so no scalability across cores.
- No hardening (see above).
- No special path for huge allocations.
- Reserves 256 MiB of address space up front and assumes 4 KiB pages.
- The `sbrk` backend is unsafe once the C library allocates between calls.
- Not a drop-in replacement for the C library.

---

## Exercises

1. Implement `my_calloc` (watch for overflow in `nmemb * size`).
2. Implement `my_realloc` (grow in place if the next block is free, otherwise
   allocate, copy and free).
3. Add separate free lists per size class.
4. Allocate very large blocks directly with `mmap`/`VirtualAlloc`, outside the
   heap.
5. Add canaries and guard pages, then try to detect an overflow.
6. Give each thread its own arena with `_Thread_local`.
7. Benchmark against the C library with mixed allocation sizes.
8. Build with `-DMYMALLOC_USE_SBRK=ON` on Linux and interleave C library
   allocations with ours. Explain exactly what breaks and why.

---

## Project layout

```text
my-malloc/
├── CMakeLists.txt
├── LICENSE
├── README.md
├── .github/workflows/ci.yml
├── include/mymalloc.h        public API
├── src/
│   ├── mm_os.h               OS interface
│   ├── mm_os.c               VirtualAlloc / mmap / sbrk backends
│   └── mymalloc.c            the allocator
├── apps/demo.c               step-by-step demonstration
└── tests/test_mymalloc.c     nine tests, one process each
```

---

## Troubleshooting

- **MSYS2: `cc1` fails with exit code 1 and no message.** `C:\msys64\ucrt64\bin`
  is not on `PATH`. Add it before running CMake.
- **CMake/GCC mangle the project path** (`d├®faut`, `dÚfaut`). Avoid non-ASCII
  characters in the path. `C:\dev\my-malloc` always works.
- **`MYMALLOC_USE_SBRK=ON` fails on Windows.** Expected: the option is POSIX-only,
  and CMake stops with an explicit message.
- **`stress` is slow in Debug.** It performs 20,000 operations with periodic
  invariant checks. Use a Release build when you care about speed.

---

## License

MIT — see [`LICENSE`](LICENSE).

The article and its original code belong to their author, Alejandro Nadal. This
repository is an independent teaching reimplementation, not a copy of the
article. It is not affiliated with the author or with Level Up Coding.
