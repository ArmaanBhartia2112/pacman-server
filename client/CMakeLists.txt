cmake_minimum_required(VERSION 3.16)

# Try to find SDL2 - gracefully skip if not available
find_package(SDL2 QUIET)

if(SDL2_FOUND OR SDL2_INCLUDE_DIRS)
    add_executable(pacman_client
        main.c
        client.c
        render.c
        input.c
        interp.c
    )
    target_link_libraries(pacman_client PRIVATE protocol SDL2::SDL2 m)
    target_include_directories(pacman_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/protocol
    )
    target_compile_options(pacman_client PRIVATE -Wall -Wextra -O2)
    message(STATUS "SDL2 found — building pacman_client")
else()
    message(WARNING "SDL2 not found — skipping pacman_client build. Install SDL2 with: brew install sdl2")
endif()
