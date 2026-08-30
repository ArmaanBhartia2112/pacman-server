#include "client.h"
#include "render.h"
#include "input.h"
#include "interp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#define TARGET_FPS   60
#define FRAME_MS     (1000 / TARGET_FPS)

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [--host HOST] [--tcp-port PORT] [--udp-port PORT]"
            " [--name NAME]\n"
            "Defaults: host=127.0.0.1 tcp=7777 udp=7778 name=Player\n",
            prog);
}

int main(int argc, char **argv) {
    const char *host     = "127.0.0.1";
    int         tcp_port = 7777;
    int         udp_port = 7778;
    const char *name     = "Player";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i+1 < argc)       host     = argv[++i];
        else if (strcmp(argv[i], "--tcp-port") == 0 && i+1 < argc) tcp_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--udp-port") == 0 && i+1 < argc) udp_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--name") == 0 && i+1 < argc)  name     = argv[++i];
        else if (strcmp(argv[i], "--help") == 0) { usage(argv[0]); return 0; }
    }

    /* Initialize client */
    Client cl;
    if (client_connect(&cl, host, tcp_port, udp_port, name) < 0) {
        fprintf(stderr, "Failed to connect to server\n");
        return 1;
    }

    /* Initialize SDL renderer */
    RenderCtx rctx;
    if (render_init(&rctx) < 0) {
        client_disconnect(&cl);
        return 1;
    }
    rctx.my_player_id = (uint8_t)(cl.player_id >= 0 ? cl.player_id : 0);

    Direction current_dir  = DIR_LEFT;
    int       dir_changed  = 0;
    uint32_t  last_input_t = 0;

    /* Wait for WELCOME (up to 3 seconds) */
    uint32_t t0 = SDL_GetTicks();
    while (!cl.maze_ready && SDL_GetTicks() - t0 < 3000) {
        client_poll(&cl);
        if (cl.maze_ready) {
            render_set_maze(&rctx, cl.maze);
            rctx.my_player_id = (uint8_t)cl.player_id;
        }
        SDL_Delay(10);
    }

    /* Main game loop */
    int running = 1;
    while (running && cl.connected) {
        uint32_t frame_start = SDL_GetTicks();

        /* Process SDL events */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
                Direction nd = input_handle_keydown(ev.key.keysym.sym);
                if (nd != DIR_NONE) {
                    current_dir = nd;
                    dir_changed = 1;
                }
            }
        }

        /* Send input to server (rate-limited to ~20Hz) */
        uint32_t now = SDL_GetTicks();
        if (dir_changed || (now - last_input_t >= 50)) {
            client_send_input(&cl, current_dir);
            client_flush_tcp(&cl);
            last_input_t = now;
            dir_changed  = 0;
        }

        /* Poll network */
        if (client_poll(&cl) < 0) {
            fprintf(stderr, "Connection lost\n");
            break;
        }

        /* Update render context */
        if (cl.maze_ready && !rctx.maze_ready) {
            render_set_maze(&rctx, cl.maze);
            rctx.my_player_id = (uint8_t)cl.player_id;
        }
        memcpy(rctx.scores, cl.scores, sizeof(cl.scores));
        rctx.latency_us  = interp_latency_us(&cl.interp);
        rctx.udp_received = cl.udp_received;
        rctx.udp_discarded = cl.udp_discarded;

        /* Get interpolated state */
        InterpPlayer players[MAX_PLAYERS];
        InterpGhost  ghosts[MAX_GHOSTS];
        int pc = 0, gc = 0;
        interp_get(&cl.interp, proto_now_us(), players, &pc, ghosts, &gc);

        /* Render */
        render_frame(&rctx, players, pc, ghosts, gc);

        /* Game over display */
        if (cl.game_over) {
            printf("[client] Game over! Winner: player %d\n", cl.winner_id);
            SDL_Delay(3000);
            /* Reset for next game */
            cl.game_over = 0;
            memset(cl.scores, 0, sizeof(cl.scores));
        }

        /* Cap frame rate */
        uint32_t elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < FRAME_MS) SDL_Delay(FRAME_MS - elapsed);
    }

    render_destroy(&rctx);
    client_disconnect(&cl);

    printf("[client] UDP stats: received=%u discarded=%u applied=%u\n",
           cl.udp_received, cl.udp_discarded, cl.udp_applied);

    return 0;
}
