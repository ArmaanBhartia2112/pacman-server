#ifndef RENDER_H
#define RENDER_H

#include "../protocol/protocol.h"
#include "interp.h"
#include <SDL2/SDL.h>

/* Pixel dimensions */
#define RENDER_TILE_PX    20    /* pixels per maze tile */
#define WINDOW_W          (MAZE_COLS * RENDER_TILE_PX)
#define WINDOW_H          (MAZE_ROWS * RENDER_TILE_PX + 40)  /* +40 for HUD */
#define HUD_Y             (MAZE_ROWS * RENDER_TILE_PX)

typedef struct {
    SDL_Renderer *renderer;
    SDL_Window   *window;
    /* Cached maze (received on connect, static) */
    uint8_t       maze[MAZE_ROWS][MAZE_COLS];
    int           maze_ready;
    /* Score display */
    uint32_t      scores[MAX_PLAYERS];
    uint8_t       my_player_id;
    /* Latency display (microseconds) */
    uint64_t      latency_us;
    /* UDP stat display */
    uint32_t      udp_received;
    uint32_t      udp_discarded;
} RenderCtx;

int  render_init(RenderCtx *ctx);
void render_destroy(RenderCtx *ctx);

/* Set the maze from a WELCOME payload */
void render_set_maze(RenderCtx *ctx, const uint8_t maze[MAZE_ROWS][MAZE_COLS]);

/* Remove a pellet from the cached maze */
void render_eat_pellet(RenderCtx *ctx, int row, int col);

/* Draw one frame */
void render_frame(RenderCtx *ctx,
                  const InterpPlayer players[], int player_count,
                  const InterpGhost  ghosts[],  int ghost_count);

/* Convert fixed-point position to screen pixel */
static inline int fp_to_screen_x(int16_t fp) {
    return (int)fp * RENDER_TILE_PX / 16; /* TILE_SIZE=16 in protocol */
}
static inline int fp_to_screen_y(int16_t fp) {
    return (int)fp * RENDER_TILE_PX / 16;
}

#endif /* RENDER_H */
