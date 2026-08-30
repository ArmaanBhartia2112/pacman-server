#include "client_conn.h"
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

void conn_init(ClientConn *c, int tcp_fd) {
    memset(c, 0, sizeof(*c));
    c->tcp_fd    = tcp_fd;
    c->player_id = -1;
    c->state     = CONN_HANDSHAKE;
    c->recv_buf.len = 0;
    c->tcp_seq_out  = 0;
    c->udp_addr_valid = 0;
    c->send_len = 0;
    c->send_off = 0;
}

void conn_close(ClientConn *c) {
    if (c->tcp_fd >= 0) {
        close(c->tcp_fd);
        c->tcp_fd = -1;
    }
    c->state = CONN_CLOSING;
}

int conn_send_tcp(ClientConn *c,
                  uint8_t type, const void *payload, uint16_t len) {
    /* Append to send_buf */
    size_t space = FRAME_BUF_SIZE - (c->send_len - c->send_off);
    size_t needed = HEADER_SIZE + len;
    if (needed > space) {
        fprintf(stderr, "[conn %d] send buffer full\n", c->tcp_fd);
        return -1;
    }

    /* Compact buffer if needed */
    if (c->send_off > 0 && c->send_len > c->send_off) {
        memmove(c->send_buf, c->send_buf + c->send_off,
                c->send_len - c->send_off);
        c->send_len -= c->send_off;
        c->send_off  = 0;
    } else if (c->send_off > 0) {
        c->send_len = 0;
        c->send_off = 0;
    }

    int written = proto_write_msg(c->send_buf + c->send_len,
                                  FRAME_BUF_SIZE - c->send_len,
                                  type, c->tcp_seq_out++,
                                  payload, len);
    if (written < 0) return -1;
    c->send_len += (size_t)written;
    return 0;
}

int conn_flush_tcp(ClientConn *c) {
    while (c->send_off < c->send_len) {
        ssize_t n = send(c->tcp_fd,
                         c->send_buf + c->send_off,
                         c->send_len - c->send_off,
                         MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; /* try later */
            return -1;
        }
        if (n == 0) return -1;
        c->send_off += (size_t)n;
    }
    c->send_off = 0;
    c->send_len = 0;
    return 0;
}
