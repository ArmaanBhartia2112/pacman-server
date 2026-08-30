#ifndef CLIENT_CONN_H
#define CLIENT_CONN_H

#include "../protocol/protocol.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>

/* States of a TCP connection */
typedef enum {
    CONN_HANDSHAKE   = 0,  /* waiting for MSG_HELLO */
    CONN_ACTIVE      = 1,  /* authenticated, in game */
    CONN_CLOSING     = 2
} ConnState;

typedef struct {
    int           tcp_fd;
    int           player_id;      /* -1 if not yet assigned */
    ConnState     state;
    FrameBuf      recv_buf;

    /* UDP return address (filled after HELLO received) */
    struct sockaddr_in udp_addr;
    int                udp_addr_valid;

    /* Sequence counters */
    uint32_t      tcp_seq_out;    /* next seq to send on TCP */
    uint32_t      udp_seq_out;    /* next seq to send on UDP (monotonic, never reset) */
    uint32_t      udp_seq_last;   /* last UDP seq seen from server (client-side) */

    /* Instrumentation */
    uint32_t      udp_packets_received;
    uint32_t      udp_packets_discarded;

    /* Send buffer for TCP (for partial writes) */
    uint8_t       send_buf[FRAME_BUF_SIZE];
    size_t        send_len;
    size_t        send_off;
} ClientConn;

void  conn_init(ClientConn *c, int tcp_fd);
void  conn_close(ClientConn *c);

/* Queue a TCP message for sending (returns 0 on success) */
int   conn_send_tcp(ClientConn *c,
                    uint8_t type, const void *payload, uint16_t len);

/* Flush pending TCP send buffer (call after conn_send_tcp).
 * Returns 0 on success, -1 on error (connection should be closed). */
int   conn_flush_tcp(ClientConn *c);

#endif /* CLIENT_CONN_H */
