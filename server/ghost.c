#include "ghost.h"
#include <stdlib.h>
#include <string.h>

/* Ghost movement speed: tiles per second in fixed-point */
#define GHOST_SPEED_NORMAL     (TILE_SIZE * 7 / 10)   /* 0.7 tiles/sec * TILE_SIZE */
#define GHOST_SPEED_FRIGHTENED (TILE_SIZE * 4 / 10)

/* 1/16 tile units moved per millisecond */
#define SPEED_PER_MS_NORMAL    ((GHOST_SPEED_NORMAL)   / 1000 + 1)
#define SPEED_PER_MS_FRIGHT    ((GHOST_SPEED_FRIGHTENED) / 1000 + 1)

/* Accumulated fractional movement */
typedef struct { int acc; } Frac;

/* Corner targets for scatter mode (tile coords) */
static const int SCATTER_COLS[4] = {25, 2, 25, 2};
static const int SCATTER_ROWS[4] = {0,  0, 29, 29};

void ghosts_init(Ghost ghosts[MAX_GHOSTS]) {
    for (int i = 0; i < MAX_GHOSTS; i++) {
        Ghost *g = &ghosts[i];
        memset(g, 0, sizeof(*g));
        g->id          = (uint8_t)i;
        g->alive       = 1;
        g->state       = GHOST_SCATTER;
        g->state_timer = SCATTER_DURATION_MS;
        g->scatter_col = SCATTER_COLS[i];
        g->scatter_row = SCATTER_ROWS[i];
        g->dir         = DIR_UP;
        g->frightened_timer = 0;
        maze_ghost_spawn(i, &g->x, &g->y);
    }
}

/* Manhattan distance squared (tile units) */
static int dist2(int r1, int c1, int r2, int c2) {
    int dr = r1 - r2, dc = c1 - c2;
    return dr*dr + dc*dc;
}

/* Choose best direction toward (target_col, target_row), avoiding reverse and walls */
static Direction choose_dir(const Maze *m,
                             int cur_col, int cur_row,
                             int target_col, int target_row,
                             Direction cur_dir) {
    static const Direction dirs[4] = {DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT};
    static const int dr[4] = {-1, 1, 0, 0};
    static const int dc[4] = { 0, 0,-1, 1};
    /* Opposite direction */
    static const Direction opp[5] = {DIR_NONE, DIR_DOWN, DIR_UP, DIR_RIGHT, DIR_LEFT};

    Direction best  = DIR_NONE;
    int       best_d = 0x7FFFFFFF;

    for (int i = 0; i < 4; i++) {
        Direction d = dirs[i];
        if (cur_dir != DIR_NONE && d == opp[cur_dir]) continue; /* no reversing */
        int nr = cur_row + dr[i];
        int nc = cur_col + dc[i];
        if (maze_is_wall(m, nr, nc)) continue;
        int d2 = dist2(nr, nc, target_row, target_col);
        if (d2 < best_d) { best_d = d2; best = d; }
    }
    return best == DIR_NONE ? cur_dir : best;
}

/* Move ghost by `speed_fp` fixed-point units in its current direction */
static void move_ghost(Ghost *g, const Maze *m, int speed_fp) {
    static const int dx[5] = {0, 0, 0,-1, 1};
    static const int dy[5] = {0,-1, 1, 0, 0};

    int16_t nx = (int16_t)(g->x + dx[g->dir] * speed_fp);
    int16_t ny = (int16_t)(g->y + dy[g->dir] * speed_fp);

    if (!maze_collides(m, nx, ny)) {
        g->x = nx;
        g->y = ny;
    } else {
        /* Snap to tile center and pick new direction at next update */
        g->x = tile_to_fp(fp_to_tile(g->x));
        g->y = tile_to_fp(fp_to_tile(g->y));
    }

    /* Wrap tunnel (row 13 in classic Pacman) */
    if (g->x < 0)
        g->x = tile_to_fp(MAZE_COLS - 1);
    else if (fp_to_tile(g->x) >= MAZE_COLS)
        g->x = tile_to_fp(0);
}

/* Accumulator-based fractional movement: returns pixels to move this tick */
void ghosts_update(Ghost ghosts[MAX_GHOSTS], const Maze *m,
                   int16_t target_x, int16_t target_y,
                   int delta_ms) {
    int target_col = fp_to_tile(target_x);
    int target_row = fp_to_tile(target_y);

    for (int i = 0; i < MAX_GHOSTS; i++) {
        Ghost *g = &ghosts[i];
        if (!g->alive) continue;

        /* Update frightened timer */
        if (g->state == GHOST_FRIGHTENED) {
            g->frightened_timer -= delta_ms;
            if (g->frightened_timer <= 0) {
                g->state       = GHOST_SCATTER;
                g->state_timer = SCATTER_DURATION_MS;
            }
        } else {
            /* Scatter/chase cycling */
            g->state_timer -= delta_ms;
            if (g->state_timer <= 0) {
                if (g->state == GHOST_SCATTER) {
                    g->state       = GHOST_CHASE;
                    g->state_timer = CHASE_DURATION_MS;
                } else {
                    g->state       = GHOST_SCATTER;
                    g->state_timer = SCATTER_DURATION_MS;
                }
            }
        }

        /* Choose movement target based on state */
        int tc, tr;
        int speed_fp;
        if (g->state == GHOST_FRIGHTENED) {
            /* Random movement */
            tc = rand() % MAZE_COLS;
            tr = rand() % MAZE_ROWS;
            speed_fp = 1;  /* 1 fixed-point unit per tick */
        } else if (g->state == GHOST_CHASE) {
            tc = target_col;
            tr = target_row;
            speed_fp = 2;
        } else { /* SCATTER */
            tc = g->scatter_col;
            tr = g->scatter_row;
            speed_fp = 2;
        }

        /* Update direction at tile-crossing points (when centered on tile) */
        int cx = g->x % TILE_SIZE;
        int cy = g->y % TILE_SIZE;
        if (cx < speed_fp + 2 && cy < speed_fp + 2) {
            int cur_col = fp_to_tile(g->x);
            int cur_row = fp_to_tile(g->y);
            Direction new_dir = choose_dir(m, cur_col, cur_row, tc, tr, g->dir);
            if (new_dir != DIR_NONE) g->dir = new_dir;
        }

        /* Move */
        (void)delta_ms; /* speed already encoded in speed_fp per tick */
        move_ghost(g, m, speed_fp);
    }
}

void ghosts_frighten(Ghost ghosts[MAX_GHOSTS]) {
    for (int i = 0; i < MAX_GHOSTS; i++) {
        if (!ghosts[i].alive) continue;
        ghosts[i].state           = GHOST_FRIGHTENED;
        ghosts[i].frightened_timer = FRIGHTENED_DURATION_MS;
    }
}

void ghosts_fill_snapshot(const Ghost ghosts[MAX_GHOSTS],
                           GhostPos out[MAX_GHOSTS]) {
    for (int i = 0; i < MAX_GHOSTS; i++) {
        out[i].ghost_id = ghosts[i].id;
        out[i].x        = ghosts[i].x;
        out[i].y        = ghosts[i].y;
        out[i].state    = (uint8_t)ghosts[i].state;
    }
}
