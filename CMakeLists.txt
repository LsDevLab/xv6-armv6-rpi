cmake_minimum_required(VERSION 3.16)
project(xv6 C)

set(CMAKE_C_STANDARD 11)

add_compile_options(
        -ffreestanding
        -fno-builtin
)

include_directories(${CMAKE_SOURCE_DIR})

file(GLOB_RECURSE XV6_SOURCES *.c *.h)
add_library(xv6_dummy STATIC ${XV6_SOURCES})
