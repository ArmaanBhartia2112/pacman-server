#include "maze.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * Classic 28×31 Pacman maze layout
 * 0=wall, 1=pellet, 2=power pellet, 3=empty, 4=ghost house
 * ============================================================ */
static const uint8_t BASE_MAZE[MAZE_ROWS][MAZE_COLS] = {
    /* row 0  */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* row 1  */ {0,1,1,1,1,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,1,1,0},
    /* row 2  */ {0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0},
    /* row 3  */ {0,2,0,0,0,0,1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1,0,0,0,0,2,0},
    /* row 4  */ {0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0},
    /* row 5  */ {0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0},
    /* row 6  */ {0,1,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,1,0},
    /* row 7  */ {0,1,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,1,0},
    /* row 8  */ {0,1,1,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,1,1,0},
    /* row 9  */ {0,0,0,0,0,0,1,0,0,0,0,0,3,0,0,3,0,0,0,0,0,1,0,0,0,0,0,0},
    /* row 10 */ {0,0,0,0,0,0,1,0,0,0,0,0,3,0,0,3,0,0,0,0,0,1,0,0,0,0,0,0},
    /* row 11 */ {0,0,0,0,0,0,1,0,0,3,3,3,3,3,3,3,3,3,3,0,0,1,0,0,0,0,0,0},
    /* row 12 */ {0,0,0,0,0,0,1,0,0,3,0,0,0,4,4,0,0,0,3,0,0,1,0,0,0,0,0,0},
    /* row 13 */ {3,3,3,3,3,3,1,3,3,3,0,4,4,4,4,4,4,0,3,3,3,1,3,3,3,3,3,3},
    /* row 14 */ {0,0,0,0,0,0,1,0,0,3,0,4,4,4,4,4,4,0,3,0,0,1,0,0,0,0,0,0},
    /* row 15 */ {0,0,0,0,0,0,1,0,0,3,0,0,0,0,0,0,0,0,3,0,0,1,0,0,0,0,0,0},
    /* row 16 */ {0,0,0,0,0,0,1,0,0,3,3,3,3,3,3,3,3,3,3,0,0,1,0,0,0,0,0,0},
    /* row 17 */ {0,0,0,0,0,0,1,0,0,3,0,0,0,0,0,0,0,0,3,0,0,1,0,0,0,0,0,0},
    /* row 18 */ {0,0,0,0,0,0,1,0,0,3,0,0,0,0,0,0,0,0,3,0,0,1,0,0,0,0,0,0},
    /* row 19 */ {0,1,1,1,1,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,1,1,0},
    /* row 20 */ {0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0},
    /* row 21 */ {0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0},
    /* row 22 */ {0,2,1,1,0,0,1,1,1,1,1,1,1,3,3,1,1,1,1,1,1,1,0,0,1,1,2,0},
    /* row 23 */ {0,0,0,1,0,0,1,0,0,1,0,0,0,0,0,0,0,0,1,0,0,1,0,0,1,0,0,0},
    /* row 24 */ {0,0,0,1,0,0,1,0,0,1,0,0,0,0,0,0,0,0,1,0,0,1,0,0,1,0,0,0},
    /* row 25 */ {0,1,1,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,1,1,0},
    /* row 26 */ {0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0},
    /* row 27 */ {0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0},
    /* row 28 */ {0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0},
    /* row 29 */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* row 30 */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};

void maze_init(Maze *m) {
    memcpy(m->tiles, BASE_MAZE, sizeof(BASE_MAZE));
    memset(m->pellets, 0, sizeof(m->pellets));
    m->total_pellets = 0;
    m->remaining_pellets = 0;

    for (int r = 0; r < MAZE_ROWS; r++) {
        for (int c = 0; c < MAZE_COLS; c++) {
            uint8_t t = m->tiles[r][c];
            if (t == TILE_PELLET || t == TILE_POWER) {
                m->pellets[r][c] = 1;
                m->total_pellets++;
                m->remaining_pellets++;
            }
        }
    }
}

int maze_is_wall(const Maze *m, int row, int col) {
    if (row < 0 || row >= MAZE_ROWS || col < 0 || col >= MAZE_COLS)
        return 1;  /* out-of-bounds = wall */
    return m->tiles[row][col] == TILE_WALL;
}

int maze_eat_pellet(Maze *m, int row, int col) {
    if (row < 0 || row >= MAZE_ROWS || col < 0 || col >= MAZE_COLS) return 0;
    if (!m->pellets[row][col]) return 0;
    m->pellets[row][col] = 0;
    m->remaining_pellets--;
    return (int)m->tiles[row][col];  /* TILE_PELLET or TILE_POWER */
}

int maze_collides(const Maze *m, int16_t x, int16_t y) {
    /* Check all four corners of the entity (8x8 units) */
    int half = TILE_SIZE / 2 - 2;  /* small margin */
    int cols[2] = { fp_to_tile((int16_t)(x - half)), fp_to_tile((int16_t)(x + half)) };
    int rows[2] = { fp_to_tile((int16_t)(y - half)), fp_to_tile((int16_t)(y + half)) };
    for (int ri = 0; ri < 2; ri++)
        for (int ci = 0; ci < 2; ci++)
            if (maze_is_wall(m, rows[ri], cols[ci]))
                return 1;
    return 0;
}

/* Player spawns: spread around the start position */
void maze_player_spawn(int player_index, int16_t *x, int16_t *y) {
    /* Classic Pacman starts around (14, 23) in tile coords */
    static const int spawn_cols[] = {13, 14, 13, 14, 12, 15, 12, 15};
    static const int spawn_rows[] = {23, 23, 22, 22, 23, 23, 22, 22};
    int idx = player_index % 8;
    *x = tile_to_fp(spawn_cols[idx]);
    *y = tile_to_fp(spawn_rows[idx]);
}

/* Ghost spawns: inside the ghost house */
void maze_ghost_spawn(int ghost_index, int16_t *x, int16_t *y) {
    static const int gcols[] = {13, 14, 12, 15};
    static const int grows[] = {14, 14, 14, 14};
    int idx = ghost_index % 4;
    *x = tile_to_fp(gcols[idx]);
    *y = tile_to_fp(grows[idx]);
}
