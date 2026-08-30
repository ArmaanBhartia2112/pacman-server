#ifndef GAME_H
#define GAME_H

#include "../protocol/protocol.h"
#include "maze.h"
#include "ghost.h"
#include <stdint.h>

#define PLAYER_SPEED_FP   2    /* fixed-point units per tick */
#define PELLET_SCORE      10
#define POWER_SCORE       50
#define GHOST_EAT_SCORE   200

typedef struct {
    uint8_t   id;
    int16_t   x, y;
    Direction dir;
    Direction pending_dir;  /* queued from client input */
    uint32_t  score;
    uint8_t   alive;
    uint8_t   authenticated;
    char      name[MAX_NAME_LEN];
    /* Instrumentation */
    uint32_t  tick_count;
    /* Spawn protection: invulnerable for first N ticks */
    uint32_t  spawn_ticks_remaining;
} Player;

typedef struct {
    Maze     maze;
    Player   players[MAX_PLAYERS];
    int      player_count;
    Ghost    ghosts[MAX_GHOSTS];
    int      game_over;
    uint8_t  winner_id;
    uint32_t tick_number;
    uint64_t last_tick_us;   /* time of last simulation tick */
    int      tick_rate_hz;   /* target tick rate */
    /* Instrumentation counters */
    uint64_t udp_sent_count;
} GameState;

/* Initialize game state */
void game_init(GameState *gs, int tick_rate_hz);

/* Add a new player. Returns player_id, or -1 if full. */
int game_add_player(GameState *gs, const char *name);

/* Remove a player (disconnect) */
void game_remove_player(GameState *gs, int player_id);

/* Queue a direction input for a player */
void game_set_input(GameState *gs, int player_id, Direction dir);

/* Run one simulation tick (delta_ms = ms since last tick).
 * Returns bitmask of events (GAME_EVENT_*) */
#define GAME_EVENT_PELLET_EATEN  (1 << 0)
#define GAME_EVENT_SCORE_CHANGED (1 << 1)
#define GAME_EVENT_GAME_OVER     (1 << 2)

typedef struct {
    int      events;
    uint8_t  pellet_row, pellet_col, pellet_is_power, pellet_player;
    uint8_t  score_player;
    uint32_t score_value;
} TickResult;

TickResult game_tick(GameState *gs, int delta_ms);

/* Build a UDP state snapshot payload */
PayloadStateSnapshot game_build_snapshot(const GameState *gs);

/* Build a GameOver payload */
PayloadGameOver game_build_game_over(const GameState *gs);

#endif /* GAME_H */
