#include <benchmark/benchmark.h>

#include <atomic>
#include <thread>
#include <vector>

#include "aegis/thread_pool.hpp"

static void BM_ProducerContention(benchmark::State& state) {
    size_t producer_threads = state.range(0);
    constexpr size_t TASKS_TOTAL = 20000;
    size_t tasks_per_producer = TASKS_TOTAL / producer_threads;

    aegis::ThreadPoolConfig config;
    config.worker_count = 4;
    aegis::ThreadPool pool(config);

    for (auto _ : state) {
        std::atomic<uint64_t> counter{0};
        std::vector<std::thread> producers;
        producers.reserve(producer_threads);

        for (size_t p = 0; p < producer_threads; ++p) {
            producers.emplace_back([&pool, &counter, tasks_per_producer]() {
                for (size_t i = 0; i < tasks_per_producer; ++i) {
                    pool.submit([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
                }
            });
        }

        for (auto& th : producers) {
            th.join();
        }

        pool.wait_for_all();
        benchmark::DoNotOptimize(counter.load());
    }

    state.SetItemsProcessed(state.iterations() * TASKS_TOTAL);
}

BENCHMARK(BM_ProducerContention)->RangeMultiplier(2)->Range(1, 16);

BENCHMARK_MAIN();
