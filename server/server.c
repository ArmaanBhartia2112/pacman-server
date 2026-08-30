#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ============================================================
 * Helpers
 * ============================================================ */
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int create_tcp_listen(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket tcp"); return -1; }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind tcp"); close(fd); return -1;
    }
    if (listen(fd, 16) < 0) { perror("listen"); close(fd); return -1; }
    set_nonblocking(fd);
    return fd;
}

static int create_udp_socket(int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket udp"); return -1; }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind udp"); close(fd); return -1;
    }
    set_nonblocking(fd);
    return fd;
}

/* ============================================================
 * Init / Destroy
 * ============================================================ */
int server_init(Server *srv, int tcp_port, int udp_port, int tick_rate_hz) {
    memset(srv, 0, sizeof(*srv));
    srv->tick_rate_hz = tick_rate_hz;
    srv->tcp_port     = tcp_port;
    srv->udp_port     = udp_port;
    srv->running      = 1;

    srv->tcp_listen_fd = create_tcp_listen(tcp_port);
    if (srv->tcp_listen_fd < 0) return -1;

    srv->udp_fd = create_udp_socket(udp_port);
    if (srv->udp_fd < 0) { close(srv->tcp_listen_fd); return -1; }

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        srv->conns[i].tcp_fd = -1;
    }

    game_init(&srv->game, tick_rate_hz);

    printf("[server] TCP port %d, UDP port %d, tick rate %dHz\n",
           tcp_port, udp_port, tick_rate_hz);
    return 0;
}

void server_destroy(Server *srv) {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (srv->conns[i].tcp_fd >= 0)
            conn_close(&srv->conns[i]);
    }
    if (srv->tcp_listen_fd >= 0) close(srv->tcp_listen_fd);
    if (srv->udp_fd >= 0)        close(srv->udp_fd);
}

/* ============================================================
 * Find free connection slot
 * ============================================================ */
static ClientConn *find_free_slot(Server *srv) {
    for (int i = 0; i < MAX_CONNECTIONS; i++)
        if (srv->conns[i].tcp_fd < 0) return &srv->conns[i];
    return NULL;
}

/* ============================================================
 * Broadcast UDP snapshot to all authenticated clients
 * ============================================================ */
static void broadcast_snapshot(Server *srv) {
    PayloadStateSnapshot snap = game_build_snapshot(&srv->game);
    /* Use a single UDP sequence counter on the server side (per-client seq) */

    uint8_t buf[sizeof(MsgHeader) + sizeof(PayloadStateSnapshot)];
    PayloadStateSnapshot snap_net = snap;
    proto_hton_snapshot(&snap_net);

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        ClientConn *c = &srv->conns[i];
        if (c->tcp_fd < 0 || c->state != CONN_ACTIVE || !c->udp_addr_valid)
            continue;

        /* Use per-client monotonic UDP seq — never resets, survives game restart */
        int n = proto_write_msg(buf, sizeof(buf),
                                MSG_STATE_SNAPSHOT,
                                c->udp_seq_out++,
                                &snap_net, sizeof(snap_net));
        if (n > 0) {
            sendto(srv->udp_fd, buf, (size_t)n, 0,
                   (struct sockaddr *)&c->udp_addr, sizeof(c->udp_addr));
            srv->game.udp_sent_count++;
        }
    }

    /* Instrumentation: record tick timestamp */
    uint64_t now = proto_now_us();
    srv->tick_timestamps[srv->tick_ts_head % 1024] = now;
    srv->tick_ts_head++;
    srv->total_ticks++;
}

/* ============================================================
 * Send events to all clients via TCP
 * ============================================================ */
static void broadcast_pellet_eaten(Server *srv, TickResult *res) {
    PayloadPelletEaten p;
    p.row       = res->pellet_row;
    p.col       = res->pellet_col;
    p.is_power  = res->pellet_is_power;
    p.player_id = res->pellet_player;
    proto_hton_pellet_eaten(&p);

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        ClientConn *c = &srv->conns[i];
        if (c->tcp_fd < 0 || c->state != CONN_ACTIVE) continue;
        conn_send_tcp(c, MSG_PELLET_EATEN, &p, sizeof(p));
    }
}

static void broadcast_score_update(Server *srv, TickResult *res) {
    PayloadScoreUpdate p;
    p.player_id = res->score_player;
    p.score     = res->score_value;
    proto_hton_score_update(&p);

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        ClientConn *c = &srv->conns[i];
        if (c->tcp_fd < 0 || c->state != CONN_ACTIVE) continue;
        conn_send_tcp(c, MSG_SCORE_UPDATE, &p, sizeof(p));
    }
}

static void broadcast_game_over(Server *srv) {
    PayloadGameOver go = game_build_game_over(&srv->game);
    proto_hton_game_over(&go);

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        ClientConn *c = &srv->conns[i];
        if (c->tcp_fd < 0 || c->state != CONN_ACTIVE) continue;
        conn_send_tcp(c, MSG_GAME_OVER, &go, sizeof(go));
        conn_flush_tcp(c);
    }
}

/* ============================================================
 * Handle a fully-received TCP message from a client
 * ============================================================ */
static void handle_tcp_msg(Server *srv, ClientConn *c,
                             uint8_t type, uint32_t seq,
                             uint8_t *payload, uint16_t plen) {
    (void)seq;

    if (type == MSG_HELLO) {
        if (c->state != CONN_HANDSHAKE) return;
        if (plen < sizeof(PayloadHello)) {
            fprintf(stderr, "[server] bad HELLO length %u\n", plen);
            goto reject;
        }
        PayloadHello hello;
        memcpy(&hello, payload, sizeof(hello));
        proto_ntoh_hello(&hello);

        if (hello.magic != PROTO_MAGIC || hello.version != PROTO_VERSION) {
            fprintf(stderr, "[server] bad magic/version\n");
            goto reject;
        }
        hello.name[MAX_NAME_LEN - 1] = '\0';

        /* Record UDP return port from the client — use same IP as TCP */
        struct sockaddr_in peer = {0};
        socklen_t plen2 = sizeof(peer);
        getpeername(c->tcp_fd, (struct sockaddr *)&peer, &plen2);
        /* Client sends on udp_port (same as server udp_port) - we'll
         * learn their actual UDP port from the first UDP packet.
         * For now store IP and use configured udp_port as placeholder. */
        c->udp_addr = peer;
        /* Port will be overwritten when first UDP datagram arrives */

        int pid = game_add_player(&srv->game, hello.name);
        if (pid < 0) goto reject;

        c->player_id   = pid;
        c->state       = CONN_ACTIVE;

        /* Send WELCOME */
        PayloadWelcome welcome;
        memset(&welcome, 0, sizeof(welcome));
        welcome.player_id     = (uint8_t)pid;
        welcome.max_players   = MAX_PLAYERS;
        welcome.tick_rate_hz  = (uint16_t)srv->tick_rate_hz;
        memcpy(welcome.maze, srv->game.maze.tiles, sizeof(welcome.maze));
        proto_hton_welcome(&welcome);
        conn_send_tcp(c, MSG_WELCOME, &welcome, sizeof(welcome));
        conn_flush_tcp(c);

        printf("[server] player %d (%s) joined\n", pid, hello.name);
        return;

    reject:;
        PayloadReject rej;
        strncpy(rej.reason, "server full or bad handshake", sizeof(rej.reason)-1);
        conn_send_tcp(c, MSG_REJECT, &rej, sizeof(rej));
        conn_flush_tcp(c);
        conn_close(c);
        return;
    }

    if (type == MSG_INPUT) {
        if (c->state != CONN_ACTIVE) return;
        if (plen < sizeof(PayloadInput)) return;
        PayloadInput inp;
        memcpy(&inp, payload, sizeof(inp));
        game_set_input(&srv->game, c->player_id, (Direction)inp.direction);
        return;
    }

    fprintf(stderr, "[server] unknown TCP msg type %u from fd %d\n",
            type, c->tcp_fd);
}

/* ============================================================
 * Handle incoming UDP packet (seq numbering, source auth)
 * ============================================================ */
static void handle_udp(Server *srv) {
    uint8_t buf[2048];
    struct sockaddr_in src = {0};
    socklen_t slen = sizeof(src);

    ssize_t n = recvfrom(srv->udp_fd, buf, sizeof(buf), 0,
                         (struct sockaddr *)&src, &slen);
    if (n <= 0) return;

    /* Find matching client by IP (any state — knock may arrive before WELCOME) */
    ClientConn *c = NULL;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (srv->conns[i].tcp_fd >= 0 &&
            srv->conns[i].udp_addr.sin_addr.s_addr == src.sin_addr.s_addr) {
            /* Prefer an already-matched port, or update if not yet valid */
            if (!srv->conns[i].udp_addr_valid ||
                srv->conns[i].udp_addr.sin_port == src.sin_port) {
                c = &srv->conns[i];
                if (!c->udp_addr_valid) {
                    c->udp_addr.sin_port = src.sin_port;
                    c->udp_addr_valid    = 1;
                    fprintf(stderr, "[server] learned UDP port %u for fd %d\n",
                            ntohs(src.sin_port), c->tcp_fd);
                }
                break;
            }
        }
    }
    if (!c) {
        /* On loopback all IPs are 127.0.0.1 — match the first unmatched conn */
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (srv->conns[i].tcp_fd >= 0 && !srv->conns[i].udp_addr_valid) {
                c = &srv->conns[i];
                c->udp_addr.sin_port  = src.sin_port;
                c->udp_addr.sin_addr  = src.sin_addr;
                c->udp_addr_valid     = 1;
                fprintf(stderr, "[server] UDP knock from new fd %d port %u\n",
                        c->tcp_fd, ntohs(src.sin_port));
                break;
            }
        }
    }
    if (!c) {
        fprintf(stderr, "[server] UDP from unknown source - discarding\n");
        return;
    }

    /* Parse header */
    if ((size_t)n < HEADER_SIZE) return;
    MsgHeader hdr;
    memcpy(&hdr, buf, HEADER_SIZE);
    hdr.seq = ntohl(hdr.seq);

    /* Client→server UDP: just a knock for address learning.
     * Ignore payload (not a defined direction — client uses TCP for inputs). */
    (void)hdr;
}

/* ============================================================
 * Accept new TCP connection
 * ============================================================ */
static void accept_connections(Server *srv) {
    for (;;) {
        struct sockaddr_in addr;
        socklen_t alen = sizeof(addr);
        int fd = accept(srv->tcp_listen_fd, (struct sockaddr *)&addr, &alen);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("accept");
            break;
        }
        set_nonblocking(fd);

        ClientConn *slot = find_free_slot(srv);
        if (!slot) {
            fprintf(stderr, "[server] no free slots — rejecting fd %d\n", fd);
            close(fd);
            continue;
        }
        conn_init(slot, fd);
        /* Store remote IP for UDP matching */
        slot->udp_addr = addr;
        srv->conn_count++;
        printf("[server] new TCP connection fd=%d from %s\n",
               fd, inet_ntoa(addr.sin_addr));
    }
}

/* ============================================================
 * Read from a TCP client fd
 * ============================================================ */
static void read_client(Server *srv, ClientConn *c) {
    uint8_t tmp[4096];
    ssize_t n = recv(c->tcp_fd, tmp, sizeof(tmp), 0);
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        /* Disconnect */
        printf("[server] client fd=%d disconnected\n", c->tcp_fd);
        if (c->player_id >= 0)
            game_remove_player(&srv->game, c->player_id);
        conn_close(c);
        srv->conn_count--;
        return;
    }

    /* Feed into framing buffer — may produce multiple complete messages */
    size_t consumed = 0;
    while (consumed < (size_t)n) {
        uint8_t type, *payload;
        uint32_t seq;
        uint16_t plen;

        size_t chunk = (size_t)n - consumed;
        int r = proto_feed(&c->recv_buf, tmp + consumed, chunk,
                           &type, &seq, &payload, &plen);
        if (r == 1) {
            handle_tcp_msg(srv, c, type, seq, payload, plen);
            consumed += chunk; /* proto_feed consumes everything added */
            /* But it might have assembled a message from previously buffered
             * bytes — we break and re-feed 0 bytes to drain any remaining */
        } else if (r == 0) {
            consumed += chunk; /* all bytes buffered, waiting for more */
        } else {
            fprintf(stderr, "[server] framing error on fd=%d\n", c->tcp_fd);
            conn_close(c);
            if (c->player_id >= 0)
                game_remove_player(&srv->game, c->player_id);
            return;
        }
        /* Try to drain any further complete messages buffered */
        while (c->tcp_fd >= 0) {
            r = proto_feed(&c->recv_buf, tmp, 0,
                           &type, &seq, &payload, &plen);
            if (r == 1) handle_tcp_msg(srv, c, type, seq, payload, plen);
            else break;
        }
        break;
    }
}

/* ============================================================
 * Main select-based event loop
 * ============================================================ */
void server_run(Server *srv) {
    long tick_us = 1000000L / srv->tick_rate_hz;
    uint64_t next_tick = proto_now_us() + (uint64_t)tick_us;

    while (srv->running) {
        /* Build fd_set */
        fd_set rset, wset;
        FD_ZERO(&rset);
        FD_ZERO(&wset);

        int maxfd = srv->tcp_listen_fd;
        FD_SET(srv->tcp_listen_fd, &rset);
        FD_SET(srv->udp_fd, &rset);
        if (srv->udp_fd > maxfd) maxfd = srv->udp_fd;

        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            ClientConn *c = &srv->conns[i];
            if (c->tcp_fd < 0) continue;
            FD_SET(c->tcp_fd, &rset);
            if (c->tcp_fd > maxfd) maxfd = c->tcp_fd;
            if (c->send_len > c->send_off)
                FD_SET(c->tcp_fd, &wset);
        }

        /* Compute timeout */
        uint64_t now = proto_now_us();
        long remaining = (long)(next_tick - now);
        if (remaining < 0) remaining = 0;

        struct timeval tv;
        tv.tv_sec  = remaining / 1000000L;
        tv.tv_usec = remaining % 1000000L;

        int nready = select(maxfd + 1, &rset, &wset, NULL, &tv);
        if (nready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        /* Accept new connections */
        if (FD_ISSET(srv->tcp_listen_fd, &rset))
            accept_connections(srv);

        /* Read UDP */
        if (FD_ISSET(srv->udp_fd, &rset))
            handle_udp(srv);

        /* Read / write per-client */
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            ClientConn *c = &srv->conns[i];
            if (c->tcp_fd < 0) continue;
            if (FD_ISSET(c->tcp_fd, &rset)) read_client(srv, c);
            if (c->tcp_fd >= 0 && FD_ISSET(c->tcp_fd, &wset)) {
                if (conn_flush_tcp(c) < 0) {
                    if (c->player_id >= 0)
                        game_remove_player(&srv->game, c->player_id);
                    conn_close(c);
                    srv->conn_count--;
                }
            }
        }

        /* Simulation tick */
        now = proto_now_us();
        if ((long)(now - next_tick) >= 0) {
            int delta_ms = (int)((now - (next_tick - (uint64_t)tick_us)) / 1000);
            if (delta_ms < 1) delta_ms = 1;

            TickResult res = game_tick(&srv->game, delta_ms);

            /* Send TCP events */
            if (res.events & GAME_EVENT_PELLET_EATEN)
                broadcast_pellet_eaten(srv, &res);
            if (res.events & GAME_EVENT_SCORE_CHANGED)
                broadcast_score_update(srv, &res);
            if (res.events & GAME_EVENT_GAME_OVER) {
                broadcast_game_over(srv);
                printf("[server] game over. Restarting...\n");
                game_init(&srv->game, srv->tick_rate_hz);
                /* Re-register all still-connected players */
                for (int ci = 0; ci < MAX_CONNECTIONS; ci++) {
                    ClientConn *cc = &srv->conns[ci];
                    if (cc->tcp_fd < 0 || cc->state != CONN_ACTIVE) continue;
                    char name_buf[MAX_NAME_LEN] = "Player";
                    int new_pid = game_add_player(&srv->game, name_buf);
                    if (new_pid >= 0) {
                        cc->player_id = new_pid;
                        /* Resend WELCOME with fresh maze */
                        PayloadWelcome w2;
                        memset(&w2, 0, sizeof(w2));
                        w2.player_id    = (uint8_t)new_pid;
                        w2.max_players  = MAX_PLAYERS;
                        w2.tick_rate_hz = (uint16_t)srv->tick_rate_hz;
                        memcpy(w2.maze, srv->game.maze.tiles, sizeof(w2.maze));
                        proto_hton_welcome(&w2);
                        conn_send_tcp(cc, MSG_WELCOME, &w2, sizeof(w2));
                    }
                }
            }

            /* UDP snapshot */
            broadcast_snapshot(srv);

            /* Flush all pending TCP sends */
            for (int i = 0; i < MAX_CONNECTIONS; i++) {
                ClientConn *c = &srv->conns[i];
                if (c->tcp_fd >= 0 && c->state == CONN_ACTIVE)
                    conn_flush_tcp(c);
            }

            next_tick += (uint64_t)tick_us;

            /* Instrumentation output every 100 ticks */
            if (srv->total_ticks % 100 == 0 && srv->total_ticks > 0) {
                fprintf(stderr, "[server] tick=%llu players=%d udp_sent=%llu\n",
                        (unsigned long long)srv->total_ticks,
                        srv->game.player_count,
                        (unsigned long long)srv->game.udp_sent_count);
            }
        }
    }
}
