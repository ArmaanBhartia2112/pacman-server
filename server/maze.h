#ifndef MAZE_H
#define MAZE_H

#include "../protocol/protocol.h"
#include <stdint.h>

/* Tile dimensions for rendering hints (pixels per tile on server = 1 unit) */
#define TILE_SIZE   16   /* fixed-point units per tile */

/* ============================================================
 * Maze state — owns pellet bitfield and tile layout
 * ============================================================ */
typedef struct {
    uint8_t  tiles[MAZE_ROWS][MAZE_COLS];  /* TileType values */
    uint8_t  pellets[MAZE_ROWS][MAZE_COLS]; /* 1 = pellet present */
    int      total_pellets;
    int      remaining_pellets;
} Maze;

/* Initialize maze to the classic Pacman layout */
void maze_init(Maze *m);

/* Returns 1 if (row,col) is a wall */
int maze_is_wall(const Maze *m, int row, int col);

/* Consume pellet at (row,col). Returns tile type consumed (0 if none) */
int maze_eat_pellet(Maze *m, int row, int col);

/* Convert fixed-point position to tile coords */
static inline int fp_to_tile(int16_t fp) { return (int)fp / TILE_SIZE; }
static inline int16_t tile_to_fp(int tile) { return (int16_t)(tile * TILE_SIZE + TILE_SIZE/2); }

/* Check if a fixed-point position occupies a wall tile */
int maze_collides(const Maze *m, int16_t x, int16_t y);

/* Player/ghost spawn positions (fixed-point) */
void maze_player_spawn(int player_index, int16_t *x, int16_t *y);
void maze_ghost_spawn(int ghost_index, int16_t *x, int16_t *y);

#endif /* MAZE_H */
