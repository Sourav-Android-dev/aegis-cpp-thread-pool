cmake_minimum_required(VERSION 3.20)
project(aegis VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AEGIS_BUILD_TESTS "Build Aegis test suite" ON)
option(AEGIS_BUILD_BENCHMARKS "Build Aegis benchmarks" ON)
option(AEGIS_BUILD_EXAMPLES "Build Aegis examples" ON)
option(AEGIS_BUILD_APPS "Build Aegis applications" ON)
option(AEGIS_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(AEGIS_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(AEGIS_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

# Compiler Warnings
set(AEGIS_WARNING_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
)

# Core Library Target
add_library(aegis STATIC
    src/logger.cpp
    src/metrics.cpp
    src/task_queue.cpp
    src/worker.cpp
    src/thread_pool.cpp
    src/scheduler.cpp
)

target_include_directories(aegis
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_options(aegis PRIVATE ${AEGIS_WARNING_FLAGS})

find_package(Threads REQUIRED)
target_link_libraries(aegis PUBLIC Threads::Threads)

# Sanitizer options
if(AEGIS_ENABLE_ASAN)
    target_compile_options(aegis PUBLIC -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(aegis PUBLIC -fsanitize=address)
endif()

if(AEGIS_ENABLE_TSAN)
    target_compile_options(aegis PUBLIC -fsanitize=thread)
    target_link_options(aegis PUBLIC -fsanitize=thread)
endif()

if(AEGIS_ENABLE_UBSAN)
    target_compile_options(aegis PUBLIC -fsanitize=undefined)
    target_link_options(aegis PUBLIC -fsanitize=undefined)
endif()

# Applications
if(AEGIS_BUILD_APPS)
    add_executable(aegis_cli apps/scheduler_cli.cpp)
    target_link_libraries(aegis_cli PRIVATE aegis)
    target_compile_options(aegis_cli PRIVATE ${AEGIS_WARNING_FLAGS})
endif()

# Examples
if(AEGIS_BUILD_EXAMPLES)
    add_executable(basic_example examples/basic_example.cpp)
    target_link_libraries(basic_example PRIVATE aegis)

    add_executable(parallel_computation examples/parallel_computation.cpp)
    target_link_libraries(parallel_computation PRIVATE aegis)

    add_executable(priority_scheduling examples/priority_scheduling.cpp)
    target_link_libraries(priority_scheduling PRIVATE aegis)

    add_executable(batch_processing examples/batch_processing.cpp)
    target_link_libraries(batch_processing PRIVATE aegis)

    add_executable(task_dependencies examples/task_dependencies.cpp)
    target_link_libraries(task_dependencies PRIVATE aegis)
endif()

# Unit Tests
if(AEGIS_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    add_executable(aegis_tests
        tests/thread_pool_tests.cpp
        tests/scheduler_tests.cpp
        tests/concurrency_tests.cpp
        tests/shutdown_tests.cpp
        tests/stress_tests.cpp
    )
    target_link_libraries(aegis_tests PRIVATE aegis GTest::gtest GTest::gtest_main)
    target_compile_options(aegis_tests PRIVATE -Wall -Wextra)

    include(GoogleTest)
    gtest_discover_tests(aegis_tests)
endif()

# Benchmarks
if(AEGIS_BUILD_BENCHMARKS)
    find_package(benchmark QUIET)
    if(NOT benchmark_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googlebenchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG v1.8.3
        )
        set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googlebenchmark)
    endif()

    add_executable(sequential_benchmark benchmarks/sequential_benchmark.cpp)
    target_link_libraries(sequential_benchmark PRIVATE aegis benchmark::benchmark)

    add_executable(thread_pool_benchmark benchmarks/thread_pool_benchmark.cpp)
    target_link_libraries(thread_pool_benchmark PRIVATE aegis benchmark::benchmark)

    add_executable(contention_benchmark benchmarks/contention_benchmark.cpp)
    target_link_libraries(contention_benchmark PRIVATE aegis benchmark::benchmark)
endif()
