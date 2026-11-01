#include <benchmark/benchmark.h>

#include <cmath>
#include <vector>

static void BM_SequentialCompute(benchmark::State& state) {
    size_t num_items = state.range(0);
    std::vector<double> data(num_items, 1.2345);

    for (auto _ : state) {
        double sum = 0.0;
        for (size_t i = 0; i < num_items; ++i) {
            sum += std::sin(data[i]) * std::cos(data[i]);
            benchmark::DoNotOptimize(sum);
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * num_items);
}

BENCHMARK(BM_SequentialCompute)->RangeMultiplier(2)->Range(1000, 64000);

BENCHMARK_MAIN();
