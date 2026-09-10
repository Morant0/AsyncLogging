cmake_minimum_required(VERSION 3.16)

project(AsyncLogging LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
