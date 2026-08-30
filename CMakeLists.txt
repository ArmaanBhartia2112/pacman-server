cmake_minimum_required(VERSION 3.16)
project(MultiplayerPacman C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Platform detection: epoll on Linux, select/kqueue fallback elsewhere
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(HAVE_EPOLL)
endif()

add_subdirectory(protocol)
add_subdirectory(server)
add_subdirectory(client)
add_subdirectory(tests)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
