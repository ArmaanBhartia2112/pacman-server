#ifndef CLIENT_H
#define CLIENT_H

#include "../protocol/protocol.h"
#include "interp.h"
#include <netinet/in.h>
#include <stdint.h>

typedef struct {
    int        tcp_fd;
    int        udp_fd;
    int        connected;
    int        player_id;   /* assigned by server */

    /* TCP framing buffer */
    FrameBuf   recv_buf;

    /* TCP send buffer */
    uint8_t    send_buf[FRAME_BUF_SIZE];
    size_t     send_len;
    size_t     send_off;
    uint32_t   tcp_seq_out;

    /* UDP sequence tracking */
    uint32_t   udp_last_seq;

    /* Server address */
    struct sockaddr_in server_udp_addr;

    /* Snapshot interpolator */
    Interpolator interp;

    /* Cached maze (received on WELCOME) */
    uint8_t    maze[MAZE_ROWS][MAZE_COLS];
    int        maze_ready;
    uint16_t   server_tick_rate;

    /* Scores */
    uint32_t   scores[MAX_PLAYERS];

    /* Instrumentation */
    uint32_t   udp_received;
    uint32_t   udp_discarded;  /* stale seq packets discarded */
    uint32_t   udp_applied;    /* packets actually used */

    /* Game state */
    int        game_over;
    uint8_t    winner_id;

    /* Player name */
    char       name[MAX_NAME_LEN];
} Client;

/* Connect to server. Returns 0 on success. */
int  client_connect(Client *cl, const char *host,
                    int tcp_port, int udp_port, const char *name);

/* Send a direction input to server (TCP) */
int  client_send_input(Client *cl, Direction dir);

/* Flush pending TCP send buffer */
int  client_flush_tcp(Client *cl);

/* Poll for incoming data (non-blocking). Call every frame.
 * Returns -1 if connection died. */
int  client_poll(Client *cl);

/* Close connection */
void client_disconnect(Client *cl);

#endif /* CLIENT_H */
