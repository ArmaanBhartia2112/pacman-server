#ifndef SERVER_H
#define SERVER_H

#include "game.h"
#include "client_conn.h"
#include <stdint.h>

#define MAX_CONNECTIONS  MAX_PLAYERS

typedef struct {
    /* Listening TCP socket */
    int  tcp_listen_fd;
    /* Shared UDP socket */
    int  udp_fd;
    /* Per-client connections */
    ClientConn conns[MAX_CONNECTIONS];
    int        conn_count;
    /* Game state */
    GameState  game;
    /* Config */
    int        tick_rate_hz;
    int        tcp_port;
    int        udp_port;
    /* Instrumentation */
    uint64_t   tick_timestamps[1024];  /* ring buffer */
    int        tick_ts_head;
    uint64_t   total_ticks;
    /* Running flag */
    int        running;
} Server;

/* Initialize server (bind sockets) */
int  server_init(Server *srv, int tcp_port, int udp_port, int tick_rate_hz);

/* Main event loop (blocks until game over or signal) */
void server_run(Server *srv);

/* Clean up */
void server_destroy(Server *srv);

#endif /* SERVER_H */
