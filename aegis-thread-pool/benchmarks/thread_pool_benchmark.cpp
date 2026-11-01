#include <benchmark/benchmark.h>

#include <cmath>
#include <vector>

#include "aegis/thread_pool.hpp"

static void BM_ThreadPoolScaling(benchmark::State& state) {
    size_t worker_count = state.range(0);
    constexpr size_t TASK_COUNT = 10000;

    aegis::ThreadPoolConfig config;
    config.worker_count = worker_count;
    aegis::ThreadPool pool(config);

    for (auto _ : state) {
        std::vector<std::future<double>> futures;
        futures.reserve(TASK_COUNT);

        for (size_t i = 0; i < TASK_COUNT; ++i) {
            futures.push_back(pool.submit([]() {
                double val = 1.2345;
                for (int j = 0; j < 50; ++j) {
                    val = std::sin(val) * std::cos(val) + 0.1;
                }
                return val;
            }));
        }

        for (auto& f : futures) {
            benchmark::DoNotOptimize(f.get());
        }
    }

    state.SetItemsProcessed(state.iterations() * TASK_COUNT);
}

BENCHMARK(BM_ThreadPoolScaling)->DenseRange(1, 8, 1);

BENCHMARK_MAIN();
