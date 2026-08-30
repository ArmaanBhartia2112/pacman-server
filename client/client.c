#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ============================================================
 * Connect to server
 * ============================================================ */
int client_connect(Client *cl, const char *host,
                   int tcp_port, int udp_port, const char *name) {
    memset(cl, 0, sizeof(*cl));
    interp_init(&cl->interp);
    strncpy(cl->name, name, MAX_NAME_LEN - 1);
    cl->player_id   = -1;
    cl->udp_last_seq = 0;

    /* Resolve hostname */
    struct hostent *he = gethostbyname(host);
    if (!he) { fprintf(stderr, "gethostbyname failed\n"); return -1; }

    /* TCP connect */
    cl->tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (cl->tcp_fd < 0) { perror("socket tcp"); return -1; }

    struct sockaddr_in saddr = {0};
    saddr.sin_family = AF_INET;
    memcpy(&saddr.sin_addr, he->h_addr, (size_t)he->h_length);
    saddr.sin_port = htons((uint16_t)tcp_port);

    if (connect(cl->tcp_fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        perror("connect"); close(cl->tcp_fd); cl->tcp_fd = -1; return -1;
    }
    set_nonblocking(cl->tcp_fd);

    /* UDP socket */
    cl->udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (cl->udp_fd < 0) { perror("socket udp"); close(cl->tcp_fd); return -1; }
    set_nonblocking(cl->udp_fd);

    /* Bind UDP to any port so server can send to us */
    struct sockaddr_in udp_local = {0};
    udp_local.sin_family      = AF_INET;
    udp_local.sin_addr.s_addr = INADDR_ANY;
    udp_local.sin_port        = 0;  /* OS picks */
    bind(cl->udp_fd, (struct sockaddr *)&udp_local, sizeof(udp_local));

    /* Store server UDP address */
    cl->server_udp_addr = saddr;
    cl->server_udp_addr.sin_port = htons((uint16_t)udp_port);

    /* Send HELLO */
    PayloadHello hello;
    memset(&hello, 0, sizeof(hello));
    hello.magic   = PROTO_MAGIC;
    hello.version = PROTO_VERSION;
    strncpy(hello.name, name, MAX_NAME_LEN - 1);
    proto_hton_hello(&hello);

    uint8_t buf[HEADER_SIZE + sizeof(hello)];
    int n = proto_write_msg(buf, sizeof(buf), MSG_HELLO, cl->tcp_seq_out++,
                            &hello, sizeof(hello));
    if (n > 0) send(cl->tcp_fd, buf, (size_t)n, MSG_NOSIGNAL);

    /* Send a UDP "knock" to the server so it learns our UDP src port.
     * We reuse the HELLO payload — server ignores unknown UDP types. */
    sendto(cl->udp_fd, buf, (size_t)n, 0,
           (struct sockaddr *)&cl->server_udp_addr, sizeof(cl->server_udp_addr));

    cl->connected = 1;
    printf("[client] connected to %s tcp=%d udp=%d\n", host, tcp_port, udp_port);
    return 0;
}

/* ============================================================
 * Send direction input
 * ============================================================ */
int client_send_input(Client *cl, Direction dir) {
    PayloadInput inp;
    inp.direction = (uint8_t)dir;

    /* Append to send buffer */
    size_t needed = HEADER_SIZE + sizeof(inp);
    if (cl->send_len - cl->send_off + needed > FRAME_BUF_SIZE) return -1;

    if (cl->send_off > 0 && cl->send_len > cl->send_off) {
        memmove(cl->send_buf, cl->send_buf + cl->send_off, cl->send_len - cl->send_off);
        cl->send_len -= cl->send_off;
        cl->send_off  = 0;
    } else if (cl->send_off > 0) {
        cl->send_len = 0; cl->send_off = 0;
    }

    int written = proto_write_msg(cl->send_buf + cl->send_len,
                                  FRAME_BUF_SIZE - cl->send_len,
                                  MSG_INPUT, cl->tcp_seq_out++,
                                  &inp, sizeof(inp));
    if (written < 0) return -1;
    cl->send_len += (size_t)written;
    return 0;
}

int client_flush_tcp(Client *cl) {
    while (cl->send_off < cl->send_len) {
        ssize_t n = send(cl->tcp_fd,
                         cl->send_buf + cl->send_off,
                         cl->send_len - cl->send_off,
                         MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
        if (n == 0) return -1;
        cl->send_off += (size_t)n;
    }
    cl->send_off = 0;
    cl->send_len = 0;
    return 0;
}

/* ============================================================
 * Handle a complete TCP message from server
 * ============================================================ */
static void handle_tcp_msg(Client *cl, uint8_t type,
                            uint8_t *payload, uint16_t plen) {
    switch (type) {
        case MSG_WELCOME: {
            if (plen < sizeof(PayloadWelcome)) break;
            PayloadWelcome w;
            memcpy(&w, payload, sizeof(w));
            proto_ntoh_welcome(&w);
            cl->player_id        = w.player_id;
            cl->server_tick_rate = w.tick_rate_hz;
            memcpy(cl->maze, w.maze, sizeof(w.maze));
            cl->maze_ready = 1;
            printf("[client] welcomed as player %d, tick rate %dHz\n",
                   cl->player_id, cl->server_tick_rate);
            break;
        }
        case MSG_REJECT: {
            PayloadReject rej;
            if (plen >= sizeof(rej)) {
                memcpy(&rej, payload, sizeof(rej));
                rej.reason[sizeof(rej.reason)-1] = '\0';
                fprintf(stderr, "[client] rejected: %s\n", rej.reason);
            }
            cl->connected = 0;
            break;
        }
        case MSG_PELLET_EATEN: {
            if (plen < sizeof(PayloadPelletEaten)) break;
            PayloadPelletEaten pe;
            memcpy(&pe, payload, sizeof(pe));
            proto_ntoh_pellet_eaten(&pe);
            /* Update our local maze cache */
            if (pe.row < MAZE_ROWS && pe.col < MAZE_COLS)
                cl->maze[pe.row][pe.col] = 3; /* TILE_EMPTY */
            break;
        }
        case MSG_SCORE_UPDATE: {
            if (plen < sizeof(PayloadScoreUpdate)) break;
            PayloadScoreUpdate su;
            memcpy(&su, payload, sizeof(su));
            proto_ntoh_score_update(&su);
            if (su.player_id < MAX_PLAYERS)
                cl->scores[su.player_id] = su.score;
            break;
        }
        case MSG_GAME_OVER: {
            if (plen < sizeof(PayloadGameOver)) break;
            PayloadGameOver go;
            memcpy(&go, payload, sizeof(go));
            proto_ntoh_game_over(&go);
            cl->game_over = 1;
            cl->winner_id = go.winner_id;
            printf("[client] game over! winner=%d\n", go.winner_id);
            break;
        }
        default:
            fprintf(stderr, "[client] unknown TCP msg type %u\n", type);
    }
}

/* ============================================================
 * Poll for incoming data
 * ============================================================ */
int client_poll(Client *cl) {
    /* Read TCP */
    {
        uint8_t tmp[4096];
        ssize_t n = recv(cl->tcp_fd, tmp, sizeof(tmp), 0);
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            fprintf(stderr, "[client] TCP recv error\n");
            return -1;
        }
        if (n == 0) {
            fprintf(stderr, "[client] server closed connection\n");
            return -1;
        }
        if (n > 0) {
            size_t consumed = 0;
            while (consumed < (size_t)n) {
                uint8_t type, *payload;
                uint32_t seq;
                uint16_t plen;
                size_t chunk = (size_t)n - consumed;
                int r = proto_feed(&cl->recv_buf, tmp + consumed, chunk,
                                   &type, &seq, &payload, &plen);
                (void)seq;
                if (r == 1) {
                    handle_tcp_msg(cl, type, payload, plen);
                    consumed += chunk;
                } else if (r == 0) {
                    consumed += chunk;
                } else {
                    return -1;
                }
                /* Drain any buffered complete messages */
                while (1) {
                    r = proto_feed(&cl->recv_buf, tmp, 0,
                                   &type, &seq, &payload, &plen);
                    if (r == 1) handle_tcp_msg(cl, type, payload, plen);
                    else break;
                }
                break;
            }
        }
    }

    /* Read UDP snapshots */
    {
        uint8_t buf[sizeof(MsgHeader) + sizeof(PayloadStateSnapshot) + 64];
        for (;;) {
            ssize_t n = recv(cl->udp_fd, buf, sizeof(buf), 0);
            if (n < 0) break;  /* EAGAIN — no more packets */
            if ((size_t)n < HEADER_SIZE + sizeof(PayloadStateSnapshot)) continue;

            MsgHeader hdr;
            memcpy(&hdr, buf, HEADER_SIZE);
            uint32_t seq = ntohl(hdr.seq);

            cl->udp_received++;

            /* UDP sequence check: discard stale */
            if (cl->udp_received > 1 && seq <= cl->udp_last_seq) {
                cl->udp_discarded++;
                fprintf(stderr, "[client] UDP stale seq %u <= last %u — discarded\n",
                        seq, cl->udp_last_seq);
                continue;
            }
            cl->udp_last_seq = seq;

            /* Deserialize snapshot */
            PayloadStateSnapshot snap;
            memcpy(&snap, buf + HEADER_SIZE, sizeof(snap));
            proto_ntoh_snapshot(&snap);

            /* Push to interpolator */
            uint64_t now = proto_now_us();
            interp_push(&cl->interp, &snap, now);
            cl->udp_applied++;
        }
    }

    return 0;
}

void client_disconnect(Client *cl) {
    if (cl->tcp_fd >= 0) { close(cl->tcp_fd); cl->tcp_fd = -1; }
    if (cl->udp_fd >= 0) { close(cl->udp_fd); cl->udp_fd = -1; }
    cl->connected = 0;
}
