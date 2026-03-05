// bench_simd.cpp — focused benchmark for fuzzy_score_5_simd
//
// Build:
//   g++ -O2 -march=native -std=c++20 -fno-omit-frame-pointer
//       bench_simd.cpp fuzzy.cpp -o bench_simd
//
// Run (full perf suite):
//   sudo perf stat -e cycles,instructions,cache-misses,branch-misses,
//       L1-dcache-load-misses,LLC-load-misses,stalled-cycles-backend
//       ./bench_simd
//
// Record flamegraph:
//   sudo perf record -e cycles:u --call-graph dwarf -F 999 ./bench_simd
//   sudo perf report
//
// Record cache-miss flamegraph:
//   sudo perf record -e LLC-load-misses:u --call-graph dwarf ./bench_simd
//   sudo perf report

#include "config.h"
#include "fuzzy.h"
#include "indexer.h"

#include <asm/unistd.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Lightweight PMU gate — wraps a region of code so that perf counters only
// accumulate during the scoring loop, not during filesystem scan / setup.
//
// On hybrid CPUs (Intel Alder Lake / Raptor Lake) many PMU counters are only
// available on P-cores, not E-cores.  Set PIN_TO_CPU to a P-core number to
// avoid "counter not available" errors.  -1 means "follow the thread".
//
// Usage:
//   PmuGate pmu;
//   pmu.enable();
//   ... hot loop ...
//   pmu.disable();
//   pmu.print();
// ---------------------------------------------------------------------------
constexpr int PIN_TO_CPU = -1; // set to a P-core index on hybrid Intel CPUs

// Helper: build a PERF_TYPE_HW_CACHE config word without signed-bitwise UB.
static constexpr uint64_t hw_cache_config(perf_hw_cache_id cache,
                                          perf_hw_cache_op_id op,
                                          perf_hw_cache_op_result_id result)
{
    return static_cast<uint64_t>(cache) |
           (static_cast<uint64_t>(op) << 8U) |
           (static_cast<uint64_t>(result) << 16U);
}

struct PmuGate {
    struct Counter {
        const char *name;
        uint32_t type;
        uint64_t config;
        int fd = -1;
        uint64_t value = 0;
    };

    // Add / remove events here freely.
    std::vector<Counter> counters = {
        {"cycles",          PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
        {"instructions",    PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS},
        {"cache-refs",      PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES},
        {"cache-misses",    PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES},
        {"branch-misses",   PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES},
        {"stalled-backend", PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_BACKEND},
        // L1-dcache misses
        {"L1d-misses",    PERF_TYPE_HW_CACHE,
         hw_cache_config(PERF_COUNT_HW_CACHE_L1D,
                         PERF_COUNT_HW_CACHE_OP_READ,
                         PERF_COUNT_HW_CACHE_RESULT_MISS)},
        // LLC misses
        {"LLC-load-misses", PERF_TYPE_HW_CACHE,
         hw_cache_config(PERF_COUNT_HW_CACHE_LL,
                         PERF_COUNT_HW_CACHE_OP_READ,
                         PERF_COUNT_HW_CACHE_RESULT_MISS)},
    };

    PmuGate()
    {
        for (auto &c : counters) {
            perf_event_attr attr{};
            attr.size = sizeof(attr);
            attr.type = c.type;
            attr.config = c.config;
            attr.disabled = 1; // start disabled — key for gating
            attr.exclude_kernel = 1;
            attr.exclude_hv = 1;
            // pid=0 (this thread), cpu=PIN_TO_CPU (-1 = any, or a P-core
            // index on hybrid Intel to avoid E-core PMU limitations).
            // inherit=0 so child threads don't pollute.
            c.fd = static_cast<int>(
                syscall(__NR_perf_event_open, &attr, 0 /*self*/, PIN_TO_CPU, -1, 0));

            if (c.fd < 0) {
                fprintf(stderr,
                        "  [pmu] could not open %s (need CAP_PERFMON or "
                        "perf_event_paranoid<=1): %s\n",
                        c.name, strerror(errno));
            }
        }
    }

    ~PmuGate()
    {
        for (auto &c : counters) {
            if (c.fd >= 0) {
                close(c.fd);
            }
        }
    }

    void enable()
    {
        for (auto &c : counters) {
            if (c.fd >= 0) {
                ioctl(c.fd, PERF_EVENT_IOC_RESET, 0);
            }
        }
        for (auto &c : counters) {
            if (c.fd >= 0) {
                ioctl(c.fd, PERF_EVENT_IOC_ENABLE, 0);
            }
        }
    }

    void disable()
    {
        for (auto &c : counters) {
            if (c.fd >= 0) {
                ioctl(c.fd, PERF_EVENT_IOC_DISABLE, 0);
            }
        }
        for (auto &c : counters) {
            if (c.fd >= 0) {
                const auto __attribute_maybe_unused__ _ = read(c.fd, &c.value, sizeof(c.value));
            }
        }
    }

    void print(size_t n_paths, size_t n_queries) const
    {
        const uint64_t total_calls = n_paths * n_queries;
        uint64_t cycles = 0, instrs = 0;
        for (const auto &c : counters) {
            if (strcmp(c.name, "cycles") == 0) {
                cycles = c.value;
            } else if (strcmp(c.name, "instructions") == 0) {
                instrs = c.value;
            }
        }

        printf("\n  %-22s  %16s  %12s\n", "event", "total", "per-call");
        printf("  %-22s  %16s  %12s\n", "----------------------",
               "----------------", "------------");
        for (const auto &c : counters) {
            if (c.fd < 0) {
                continue;
            }
            printf("  %-22s  %16zu  %12.2f\n", c.name, c.value,
                   static_cast<double>(c.value) /
                       static_cast<double>(total_calls));
        }
        if (cycles && instrs) {
            printf("\n  IPC : %.2f\n",
                   static_cast<double>(instrs) / static_cast<double>(cycles));
            printf(
                "  cache-miss rate : %.2f%%\n",
                100.0 *
                    [&] {
                        for (const auto &c : counters) {
                            if (strcmp(c.name, "cache-misses") == 0 && cycles) {
                                return static_cast<double>(c.value);
                            }
                        }
                        return 0.0;
                    }() /
                    [&] {
                        for (const auto &c : counters) {
                            if (strcmp(c.name, "cache-refs") == 0) {
                                return static_cast<double>(c.value);
                            }
                        }
                        return 1.0;
                    }());
        }
    }
};

// ---------------------------------------------------------------------------
// Prevent the compiler from optimising away scoring results.
// A volatile sink is more portable than __asm__ clobbers.
// ---------------------------------------------------------------------------
static volatile float g_sink = 0.0F;

int main()
{
    // -----------------------------------------------------------------------
    // SETUP PHASE — not measured by PMU
    // -----------------------------------------------------------------------
    printf("=== bench_simd: fuzzy_score_5_simd focused benchmark ===\n\n");

    const auto [config, warnings] = load_config(Config::default_path());

    printf("Scanning filesystem (roots: %zu)...\n", config.index_roots.size());
    auto paths = indexer::scan_filesystem_parallel(
        config.index_roots, config.ignore_dirs, config.ignore_dir_names);
    printf("Indexed %zu paths.\n\n", paths.size());

    // Representative queries — short strings like real launcher input
    const std::vector<std::string> queries = {
        "main", "src", "config", "test", "index",
        "read", "mk",  "json",   "cpp",  "sh",
    };

    // Optional: warm up instruction/data caches with one cold pass
    // so the first timed run isn't penalised for cold iTLB.
    printf("Warming up...\n");
    for (const auto &q : queries) {
        for (const auto &p : paths) {
            g_sink += fuzzy::fuzzy_score_5_simd(p, q);
        }
    }

    // -----------------------------------------------------------------------
    // MEASURED PHASE — PMU counters only accumulate here
    // -----------------------------------------------------------------------
    PmuGate pmu;

    printf("Starting measured run (%zu paths x %zu queries = %zu calls)...\n",
           paths.size(), queries.size(), paths.size() * queries.size());

    const auto t0 = std::chrono::steady_clock::now();
    pmu.enable(); // <<< PMU starts here

    float acc = 0.0F; // accumulate to prevent dead-code elimination
    for (const auto &q : queries) {
        for (const auto &p : paths) {
            acc += fuzzy::fuzzy_score_5_simd(p, q);
        }
    }

    pmu.disable(); // <<< PMU stops here
    const auto t1 = std::chrono::steady_clock::now();

    g_sink = acc; // ensure acc is live

    // -----------------------------------------------------------------------
    // REPORT
    // -----------------------------------------------------------------------
    const auto wall_us =
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const size_t total_calls = paths.size() * queries.size();

    printf("\n--- Wall time ---\n");
    printf("  Total  : %.2f ms\n", static_cast<double>(wall_us) / 1000.0);
    printf("  Per call: %.1f ns\n", 1000.0 * static_cast<double>(wall_us) /
                                        static_cast<double>(total_calls));
    printf("  Throughput: %.2f M paths/s\n",
           static_cast<double>(total_calls) / static_cast<double>(wall_us));

    printf("\n--- PMU counters (scoring loop only) ---");
    pmu.print(paths.size(), queries.size());

    printf("\n(g_sink=%f to prevent dead-code elimination)\n", (double)g_sink);
    return 0;
}