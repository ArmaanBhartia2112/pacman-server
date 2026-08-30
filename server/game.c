#include "game.h"
#include <string.h>
#include <stdio.h>

void game_init(GameState *gs, int tick_rate_hz) {
    memset(gs, 0, sizeof(*gs));
    maze_init(&gs->maze);
    ghosts_init(gs->ghosts);
    gs->tick_rate_hz = tick_rate_hz;
    gs->last_tick_us = proto_now_us();
    gs->winner_id    = 0xFF;

    /* All players slots empty initially */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        gs->players[i].id            = (uint8_t)i;
        gs->players[i].alive         = 0;
        gs->players[i].authenticated = 0;
    }
}

int game_add_player(GameState *gs, const char *name) {
    if (gs->player_count >= MAX_PLAYERS) return -1;
    /* Find empty slot */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &gs->players[i];
        if (!p->authenticated) {
            p->id            = (uint8_t)i;
            p->alive         = 1;
            p->authenticated = 1;
            p->score         = 0;
            p->dir           = DIR_LEFT;
            p->pending_dir   = DIR_LEFT;
            p->tick_count    = 0;
            p->spawn_ticks_remaining = 75;  /* ~3 seconds of invulnerability */
            strncpy(p->name, name, MAX_NAME_LEN - 1);
            p->name[MAX_NAME_LEN - 1] = '\0';
            maze_player_spawn(i, &p->x, &p->y);
            gs->player_count++;
            return i;
        }
    }
    return -1;
}

void game_remove_player(GameState *gs, int player_id) {
    if (player_id < 0 || player_id >= MAX_PLAYERS) return;
    Player *p = &gs->players[player_id];
    if (!p->authenticated) return;
    p->alive         = 0;
    p->authenticated = 0;
    gs->player_count--;
}

void game_set_input(GameState *gs, int player_id, Direction dir) {
    if (player_id < 0 || player_id >= MAX_PLAYERS) return;
    gs->players[player_id].pending_dir = dir;
}

/* Move a player in its direction, wall-checking */
static void move_player(Player *p, const Maze *m) {
    static const int dx[5] = {0, 0, 0,-1, 1};
    static const int dy[5] = {0,-1, 1, 0, 0};

    /* Try to apply pending direction first */
    int16_t px = (int16_t)(p->x + dx[p->pending_dir] * PLAYER_SPEED_FP);
    int16_t py = (int16_t)(p->y + dy[p->pending_dir] * PLAYER_SPEED_FP);
    if (!maze_collides(m, px, py)) {
        p->dir = p->pending_dir;
        p->x   = px;
        p->y   = py;
        return;
    }

    /* Otherwise continue in current direction */
    px = (int16_t)(p->x + dx[p->dir] * PLAYER_SPEED_FP);
    py = (int16_t)(p->y + dy[p->dir] * PLAYER_SPEED_FP);
    if (!maze_collides(m, px, py)) {
        p->x = px;
        p->y = py;
    }

    /* Tunnel wrapping (row 13, col -1 and col 28) */
    if (p->x < 0) p->x = tile_to_fp(MAZE_COLS - 1);
    else if (fp_to_tile(p->x) >= MAZE_COLS) p->x = tile_to_fp(0);
}

/* Check if two entities overlap (within half a tile) */
static int overlaps(int16_t ax, int16_t ay, int16_t bx, int16_t by) {
    int dx = (int)ax - (int)bx;
    int dy = (int)ay - (int)by;
    return (dx*dx + dy*dy) < (TILE_SIZE * TILE_SIZE / 2);
}

TickResult game_tick(GameState *gs, int delta_ms) {
    TickResult result;
    memset(&result, 0, sizeof(result));

    if (gs->game_over) return result;

    /* Find a live player to use as ghost AI target (nearest) */
    int16_t target_x = tile_to_fp(MAZE_COLS / 2);
    int16_t target_y = tile_to_fp(MAZE_ROWS / 2);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (gs->players[i].alive && gs->players[i].authenticated) {
            target_x = gs->players[i].x;
            target_y = gs->players[i].y;
            break;
        }
    }

    /* Move ghosts */
    ghosts_update(gs->ghosts, &gs->maze, target_x, target_y, delta_ms);

    /* Move players and check collisions */
    int all_dead = 1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &gs->players[i];
        if (!p->alive || !p->authenticated) continue;
        all_dead = 0;

        move_player(p, &gs->maze);
        p->tick_count++;
        if (p->spawn_ticks_remaining > 0) p->spawn_ticks_remaining--;

        /* Pellet collision */
        int row = fp_to_tile(p->y);
        int col = fp_to_tile(p->x);
        int eaten = maze_eat_pellet(&gs->maze, row, col);
        if (eaten) {
            int is_power = (eaten == TILE_POWER);
            uint32_t score_add = is_power ? POWER_SCORE : PELLET_SCORE;
            p->score += score_add;

            result.events       |= GAME_EVENT_PELLET_EATEN | GAME_EVENT_SCORE_CHANGED;
            result.pellet_row    = (uint8_t)row;
            result.pellet_col    = (uint8_t)col;
            result.pellet_is_power = (uint8_t)is_power;
            result.pellet_player = p->id;
            result.score_player  = p->id;
            result.score_value   = p->score;

            if (is_power) ghosts_frighten(gs->ghosts);
        }

        /* Ghost collision */
        for (int g = 0; g < MAX_GHOSTS; g++) {
            Ghost *gh = &gs->ghosts[g];
            if (!gh->alive) continue;
            if (overlaps(p->x, p->y, gh->x, gh->y)) {
                if (gh->state == GHOST_FRIGHTENED) {
                    /* Eat ghost */
                    gh->alive = 0;
                    p->score += GHOST_EAT_SCORE;
                    result.events      |= GAME_EVENT_SCORE_CHANGED;
                    result.score_player = p->id;
                    result.score_value  = p->score;
                } else if (p->spawn_ticks_remaining == 0) {
                    /* Player dies (no spawn protection) */
                    p->alive = 0;
                    fprintf(stderr, "[game] player %d killed by ghost %d\n", i, g);
                }
            }
        }
    }

    /* Game-over conditions */
    if (all_dead || gs->maze.remaining_pellets == 0) {
        gs->game_over = 1;
        result.events |= GAME_EVENT_GAME_OVER;
        /* Find highest scorer */
        uint32_t best = 0;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (gs->players[i].authenticated && gs->players[i].score > best) {
                best = gs->players[i].score;
                gs->winner_id = (uint8_t)i;
            }
        }
    }

    gs->tick_number++;
    return result;
}

PayloadStateSnapshot game_build_snapshot(const GameState *gs) {
    PayloadStateSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.server_time_us = proto_now_us();
    snap.ghost_count    = MAX_GHOSTS;

    int pc = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const Player *p = &gs->players[i];
        if (!p->authenticated) continue;
        PlayerPos *pp    = &snap.players[pc];
        pp->player_id    = p->id;
        pp->x            = p->x;
        pp->y            = p->y;
        pp->direction    = (uint8_t)p->dir;
        pp->alive        = p->alive;
        pc++;
    }
    snap.player_count = (uint8_t)pc;

    ghosts_fill_snapshot(gs->ghosts, snap.ghosts);
    return snap;
}

PayloadGameOver game_build_game_over(const GameState *gs) {
    PayloadGameOver go;
    memset(&go, 0, sizeof(go));
    go.winner_id = gs->winner_id;
    for (int i = 0; i < MAX_PLAYERS; i++)
        go.scores[i] = gs->players[i].score;
    return go;
}
