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
