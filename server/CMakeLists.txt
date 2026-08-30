cmake_minimum_required(VERSION 3.16)
add_executable(pacman_server
    main.c
    server.c
    game.c
    ghost.c
    maze.c
    client_conn.c
)
target_link_libraries(pacman_server PRIVATE protocol)
target_include_directories(pacman_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/protocol
)
target_compile_options(pacman_server PRIVATE -Wall -Wextra -O2)
