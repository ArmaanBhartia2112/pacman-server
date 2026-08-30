#include "render.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Player colors (RGBA) */
static const SDL_Color PLAYER_COLORS[MAX_PLAYERS] = {
    {255, 255,   0, 255},  /* yellow  */
    {  0, 200, 255, 255},  /* cyan    */
    {255, 100, 100, 255},  /* red     */
    {100, 255, 100, 255},  /* green   */
    {255, 165,   0, 255},  /* orange  */
    {200, 100, 255, 255},  /* purple  */
    {255, 255, 255, 255},  /* white   */
    {150, 150, 150, 255},  /* gray    */
};

/* Ghost colors */
static const SDL_Color GHOST_COLORS[MAX_GHOSTS] = {
    {255,  0,   0, 255},  /* Blinky - red    */
    {255,182, 255, 255},  /* Pinky  - pink   */
    {  0, 255,255, 255},  /* Inky   - cyan   */
    {255,165,  0, 255},  /* Clyde  - orange */
};
static const SDL_Color GHOST_FRIGHT = {0, 0, 200, 255};

/* ============================================================
 * Draw helpers
 * ============================================================ */
static void set_color(SDL_Renderer *r, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

/* Draw a filled circle */
static void draw_circle(SDL_Renderer *r, int cx, int cy, int radius) {
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = (int)sqrtf((float)(radius*radius - dy*dy));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

/* ============================================================
 * Init / Destroy
 * ============================================================ */
int render_init(RenderCtx *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    ctx->window = SDL_CreateWindow("Multiplayer Pacman",
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   WINDOW_W, WINDOW_H, 0);
    if (!ctx->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    ctx->renderer = SDL_CreateRenderer(ctx->window, -1,
                                       SDL_RENDERER_ACCELERATED |
                                       SDL_RENDERER_PRESENTVSYNC);
    if (!ctx->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    return 0;
}

void render_destroy(RenderCtx *ctx) {
    if (ctx->renderer) SDL_DestroyRenderer(ctx->renderer);
    if (ctx->window)   SDL_DestroyWindow(ctx->window);
    SDL_Quit();
}

void render_set_maze(RenderCtx *ctx, const uint8_t maze[MAZE_ROWS][MAZE_COLS]) {
    memcpy(ctx->maze, maze, MAZE_ROWS * MAZE_COLS);
    ctx->maze_ready = 1;
}

void render_eat_pellet(RenderCtx *ctx, int row, int col) {
    if (row >= 0 && row < MAZE_ROWS && col >= 0 && col < MAZE_COLS)
        ctx->maze[row][col] = 3; /* TILE_EMPTY */
}

/* ============================================================
 * Render one frame
 * ============================================================ */
void render_frame(RenderCtx *ctx,
                  const InterpPlayer players[], int player_count,
                  const InterpGhost  ghosts[],  int ghost_count) {
    SDL_Renderer *r = ctx->renderer;

    /* Background */
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);

    int T = RENDER_TILE_PX;

    /* Draw maze */
    if (ctx->maze_ready) {
        for (int row = 0; row < MAZE_ROWS; row++) {
            for (int col = 0; col < MAZE_COLS; col++) {
                int px = col * T, py = row * T;
                uint8_t tile = ctx->maze[row][col];
                switch (tile) {
                    case 0: /* wall */
                        SDL_SetRenderDrawColor(r, 33, 33, 200, 255);
                        SDL_Rect wr = {px, py, T, T};
                        SDL_RenderFillRect(r, &wr);
                        break;
                    case 1: /* pellet */
                        SDL_SetRenderDrawColor(r, 200, 180, 120, 255);
                        draw_circle(r, px + T/2, py + T/2, 2);
                        break;
                    case 2: /* power pellet */
                        SDL_SetRenderDrawColor(r, 220, 220, 80, 255);
                        draw_circle(r, px + T/2, py + T/2, 5);
                        break;
                    default:
                        break;
                }
            }
        }
    }

    /* Draw ghosts */
    for (int i = 0; i < ghost_count && i < MAX_GHOSTS; i++) {
        const InterpGhost *g = &ghosts[i];
        int sx = fp_to_screen_x(g->x);
        int sy = fp_to_screen_y(g->y);
        SDL_Color gc = (g->state == GHOST_FRIGHTENED)
                       ? GHOST_FRIGHT
                       : GHOST_COLORS[g->ghost_id % MAX_GHOSTS];
        set_color(r, gc);
        draw_circle(r, sx, sy, T/2 - 1);
        /* Ghost eyes */
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        draw_circle(r, sx - 3, sy - 2, 2);
        draw_circle(r, sx + 3, sy - 2, 2);
    }

    /* Draw players */
    for (int i = 0; i < player_count && i < MAX_PLAYERS; i++) {
        const InterpPlayer *p = &players[i];
        if (!p->alive) continue;
        int sx = fp_to_screen_x(p->x);
        int sy = fp_to_screen_y(p->y);
        SDL_Color pc = PLAYER_COLORS[p->player_id % MAX_PLAYERS];
        set_color(r, pc);
        draw_circle(r, sx, sy, T/2 - 2);
    }

    /* HUD — simple colored bar */
    SDL_SetRenderDrawColor(r, 20, 20, 20, 255);
    SDL_Rect hud = {0, HUD_Y, WINDOW_W, 40};
    SDL_RenderFillRect(r, &hud);

    /* Draw score text using simple pixel rectangles (no font needed) */
    /* We'll use a minimal approach: colored dot per player with score implied */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (ctx->scores[i] == 0 && i != ctx->my_player_id) continue;
        int bx = 4 + i * (WINDOW_W / MAX_PLAYERS);
        SDL_Color pc = PLAYER_COLORS[i];
        set_color(r, pc);
        SDL_Rect dot = {bx, HUD_Y + 8, 8, 8};
        SDL_RenderFillRect(r, &dot);
        /* Score bar: width proportional to score, max 5000 */
        int bar_w = (int)(ctx->scores[i] * (WINDOW_W/MAX_PLAYERS - 12) / 5000);
        if (bar_w > 0) {
            SDL_Rect bar = {bx + 10, HUD_Y + 10, bar_w, 6};
            SDL_RenderFillRect(r, &bar);
        }
    }

    /* Latency indicator dot (green/yellow/red) */
    uint64_t lat_ms = ctx->latency_us / 1000;
    SDL_Color lat_col = {0, 255, 0, 255};
    if (lat_ms > 100) lat_col = (SDL_Color){255, 200, 0, 255};
    if (lat_ms > 200) lat_col = (SDL_Color){255, 50, 50, 255};
    set_color(r, lat_col);
    SDL_Rect lat_dot = {WINDOW_W - 14, HUD_Y + 8, 10, 10};
    SDL_RenderFillRect(r, &lat_dot);

    SDL_RenderPresent(r);
}
