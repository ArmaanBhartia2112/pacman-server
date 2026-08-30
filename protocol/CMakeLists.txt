cmake_minimum_required(VERSION 3.16)
add_library(protocol STATIC protocol.c)
target_include_directories(protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(protocol PRIVATE -Wall -Wextra -O2)
