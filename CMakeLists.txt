cmake_minimum_required(VERSION 3.16)

project(my_malloc
    VERSION 1.0.0
    DESCRIPTION "A small educational malloc/free implementation"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(MYMALLOC_USE_SBRK "Use the article's sbrk() backend (POSIX only)" OFF)
option(MYMALLOC_BUILD_TESTS "Build the test suite" ON)

add_library(mymalloc STATIC
    src/mymalloc.c
    src/mm_os.c)

target_include_directories(mymalloc
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
    # MSVC needs an explicit opt-in for C11 <stdatomic.h> (VS 2022 17.5+).
    target_compile_options(mymalloc PRIVATE /W4 /std:c11 /experimental:c11atomics)
    target_compile_definitions(mymalloc PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(mymalloc PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()

if(MYMALLOC_USE_SBRK)
    if(WIN32)
        message(FATAL_ERROR
            "MYMALLOC_USE_SBRK is only available on POSIX systems "
            "(Linux, WSL, macOS). On Windows the mmap/VirtualAlloc backend "
            "is used instead.")
    endif()
    target_compile_definitions(mymalloc PUBLIC MYMALLOC_BACKEND_SBRK=1)
    message(STATUS "Backend: sbrk()  (the article's approach)")
else()
    message(STATUS "Backend: mmap / VirtualAlloc")
endif()

add_executable(demo apps/demo.c)
target_link_libraries(demo PRIVATE mymalloc)

if(MYMALLOC_BUILD_TESTS)
    enable_testing()

    add_executable(test_mymalloc tests/test_mymalloc.c)
    target_link_libraries(test_mymalloc PRIVATE mymalloc)

    add_test(NAME mymalloc COMMAND test_mymalloc)
endif()
