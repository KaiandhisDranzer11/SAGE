#pragma once

/**
 * SAGE Compiler Hints
 * Production-grade HFT compiler directives for nanosecond latency
 */

// Branch prediction hints (C++20)
#define SAGE_LIKELY(x)   (__builtin_expect(!!(x), 1))
#define SAGE_UNLIKELY(x) (__builtin_expect(!!(x), 0))

// Function attributes
#if defined(__GNUC__) || defined(__clang__)
    #define SAGE_HOT       [[gnu::hot]]
    #define SAGE_COLD      [[gnu::cold]]
    #define SAGE_FLATTEN   [[gnu::flatten]]
    #define SAGE_NOINLINE  [[gnu::noinline]]
    #define SAGE_ALWAYS_INLINE [[gnu::always_inline]] inline
    #define SAGE_PURE      [[gnu::pure]]
    #define SAGE_CONST     [[gnu::const]]
    #define SAGE_RESTRICT  __restrict__
#else
    #define SAGE_HOT
    #define SAGE_COLD
    #define SAGE_FLATTEN
    #define SAGE_NOINLINE
    #define SAGE_ALWAYS_INLINE inline
    #define SAGE_PURE
    #define SAGE_CONST
    #define SAGE_RESTRICT
#endif

// Memory prefetch hints
#if defined(__GNUC__) || defined(__clang__)
    // locality: 0 = non-temporal, 3 = high temporal (keep in cache)
    #define SAGE_PREFETCH_READ(addr)       __builtin_prefetch((addr), 0, 3)
    #define SAGE_PREFETCH_WRITE(addr)      __builtin_prefetch((addr), 1, 3)
    #define SAGE_PREFETCH_READ_NT(addr)    __builtin_prefetch((addr), 0, 0)
    #define SAGE_PREFETCH_WRITE_NT(addr)   __builtin_prefetch((addr), 1, 0)
#else
    #define SAGE_PREFETCH_READ(addr)
    #define SAGE_PREFETCH_WRITE(addr)
    #define SAGE_PREFETCH_READ_NT(addr)
    #define SAGE_PREFETCH_WRITE_NT(addr)
#endif

// Memory barrier
#if defined(__GNUC__) || defined(__clang__)
    #define SAGE_COMPILER_BARRIER() asm volatile("" ::: "memory")
    #define SAGE_CPU_PAUSE()        asm volatile("pause" ::: "memory")
#else
    #define SAGE_COMPILER_BARRIER() std::atomic_signal_fence(std::memory_order_seq_cst)
    #define SAGE_CPU_PAUSE()
#endif

// Cache line size
constexpr size_t SAGE_CACHE_LINE_SIZE = 64;

// Alignment macros
#define SAGE_CACHE_ALIGNED alignas(SAGE_CACHE_LINE_SIZE)

// Prevent false sharing padding
#define SAGE_PAD_TO_CACHE_LINE(type, name) \
    type name; \
    char name##_pad[SAGE_CACHE_LINE_SIZE - sizeof(type)]

