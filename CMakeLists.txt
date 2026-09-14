cmake_minimum_required(VERSION 3.16)

project(AsyncLogging LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(stage0_sync
    stages/stage0_sync.cpp
)

target_compile_options(stage0_sync PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
    >
)

add_executable(stage1_mini_pool
    stages/stage1_mini_pool.cpp
)

target_link_libraries(stage1_mini_pool PRIVATE
    Threads::Threads
)

target_compile_options(stage1_mini_pool PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
    >
)

add_executable(stage2_async_queue
    stages/stage2_async_queue.cpp
)

target_link_libraries(stage2_async_queue PRIVATE
    Threads::Threads
)

target_compile_options(stage2_async_queue PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
    >
)

add_executable(stage3_batch
    stages/stage3_batch.cpp
)

target_link_libraries(stage3_batch PRIVATE
    Threads::Threads
)

target_compile_options(stage3_batch PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
    >
)

enable_testing()

add_executable(fixed_buffer_test
    tests/fixed_buffer_test.cpp
)

target_include_directories(fixed_buffer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/final
)

target_compile_options(fixed_buffer_test PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
    >
)

add_test(
    NAME fixed_buffer_test
    COMMAND fixed_buffer_test
)

set_tests_properties(fixed_buffer_test PROPERTIES
    TIMEOUT 10
)
