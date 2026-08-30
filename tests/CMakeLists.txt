cmake_minimum_required(VERSION 3.16)
add_executable(load_test
    load_test.c
)
target_link_libraries(load_test PRIVATE protocol pthread)
target_include_directories(load_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/protocol
)
target_compile_options(load_test PRIVATE -Wall -Wextra -O2)
