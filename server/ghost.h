#ifndef GHOST_H
#define GHOST_H

#include "../protocol/protocol.h"
#include "maze.h"

/* How long ghosts stay frightened after a power pellet (ms) */
#define FRIGHTENED_DURATION_MS  8000
/* Scatter/chase cycle durations (ms) */
#define SCATTER_DURATION_MS     7000
#define CHASE_DURATION_MS       20000

typedef struct {
    uint8_t    id;
    int16_t    x, y;        /* fixed-point position */
    Direction  dir;         /* current direction */
    GhostState state;
    uint64_t   state_timer; /* ms remaining in current state */
    /* Scatter corner targets (tile coords) */
    int        scatter_col, scatter_row;
    int        frightened_timer; /* ms */
    uint8_t    alive;
} Ghost;

/* Initialize all 4 ghosts */
void ghosts_init(Ghost ghosts[MAX_GHOSTS]);

/* Run one AI step for all ghosts (delta_ms = milliseconds since last tick)
 * target_x/y: nearest player position for chase behavior */
void ghosts_update(Ghost ghosts[MAX_GHOSTS], const Maze *m,
                   int16_t target_x, int16_t target_y,
                   int delta_ms);

/* Trigger frightened state (power pellet eaten) */
void ghosts_frighten(Ghost ghosts[MAX_GHOSTS]);

/* Fill GhostPos array for protocol snapshot */
void ghosts_fill_snapshot(const Ghost ghosts[MAX_GHOSTS],
                           GhostPos out[MAX_GHOSTS]);

#endif /* GHOST_H */
