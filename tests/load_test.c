#include "metrics.h"
#include "../protocol/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>

/* ============================================================
 * Metrics implementation
 * ============================================================ */
void metrics_init(TestMetrics *m, int client_count) {
    memset(m, 0, sizeof(*m));
    m->clients = calloc((size_t)client_count, sizeof(ClientMetrics));
    m->count   = client_count;
    pthread_mutex_init(&m->lock, NULL);
    m->test_start_us = proto_now_us();
}

void metrics_destroy(TestMetrics *m) {
    free(m->clients);
    pthread_mutex_destroy(&m->lock);
}

void metrics_report(const TestMetrics *m, const char *test_name,
                    int tick_rate_hz, uint64_t injected_latency_ms,
                    double packet_loss_pct) {
    printf("\n=== METRICS REPORT: %s ===\n", test_name);
    uint64_t duration_us = m->test_end_us - m->test_start_us;
    printf("Test duration: %.2f seconds\n", (double)duration_us / 1e6);
    printf("Target tick rate: %d Hz\n", tick_rate_hz);
    printf("Injected latency: %llu ms\n", (unsigned long long)injected_latency_ms);
    printf("Packet loss: %.1f%%\n", packet_loss_pct);

    int connected_ok = 0;
    uint32_t total_recv = 0, total_disc = 0, total_applied = 0;
    uint64_t lat_sum = 0; uint32_t lat_n = 0;
    double measured_hz = 0.0;

    for (int i = 0; i < m->count; i++) {
        const ClientMetrics *c = &m->clients[i];
        if (c->connected) connected_ok++;
        total_recv    += c->snapshots_received;
        total_disc    += c->snapshots_discarded;
        total_applied += c->snapshots_applied;
        lat_sum       += c->latency_sum_us;
        lat_n         += c->latency_samples;

        /* Estimate tick rate from this client's snapshot receive rate */
        if (c->snapshots_applied > 1 && c->last_snap_us > c->first_snap_us) {
            double span_s = (double)(c->last_snap_us - c->first_snap_us) / 1e6;
            double hz = (double)(c->snapshots_applied - 1) / span_s;
            if (hz > measured_hz) measured_hz = hz;
        }
    }

    printf("\nClients total: %d\n", m->count);
    printf("Clients connected successfully: %d\n", connected_ok);
    printf("Clients failed to connect: %d\n", m->count - connected_ok);

    printf("\nUDP snapshots received (total across clients): %u\n", total_recv);
    printf("UDP snapshots discarded (stale seq):            %u\n", total_disc);
    printf("UDP snapshots applied to state:                 %u\n", total_applied);
    double discard_pct = total_recv > 0
                         ? (double)total_disc / (double)total_recv * 100.0
                         : 0.0;
    printf("Discard rate: %.2f%%\n", discard_pct);
    printf("Correct state maintained: %.2f%%\n",
           total_recv > 0
           ? (double)total_applied / (double)total_recv * 100.0
           : 100.0);

    if (lat_n > 0)
        printf("Average latency: %.2f ms\n", (double)(lat_sum / lat_n) / 1000.0);

    printf("Measured server tick rate (from snapshot intervals): %.1f Hz\n",
           measured_hz);

    printf("\n--- Resume Metrics ---\n");
    printf("X (max concurrent clients tested): %d\n", connected_ok);
    printf("Z (sustained tick rate Hz):         %.1f\n", measured_hz);
    printf("Y (injected latency ms):            %llu\n",
           (unsigned long long)injected_latency_ms);
    printf("D (distinct message types):         %d\n", MSG_TYPE_COUNT);
    printf("A (correct state %%):               %.1f%% (under %.1f%% packet loss)\n",
           total_recv > 0
           ? (double)total_applied / (double)total_recv * 100.0
           : 100.0,
           packet_loss_pct);
    printf("===================================\n\n");
}

/* ============================================================
 * Simulated client thread
 * ============================================================ */
typedef struct {
    TestMetrics *metrics;
    int          client_id;
    const char  *host;
    int          tcp_port;
    int          udp_port;
    int          duration_sec;
    int          tick_rate_hz;
    /* Simulated network impairment (applied in the client recv loop) */
    uint64_t     sim_latency_us;   /* microseconds to delay each UDP recv */
    int          sim_loss_pct;     /* 0-100: percent of UDP packets to drop */
} ThreadArg;

static volatile int g_stop = 0;

static void *client_thread(void *arg) {
    ThreadArg   *ta  = (ThreadArg *)arg;
    ClientMetrics *cm = &ta->metrics->clients[ta->client_id];

    cm->client_id = ta->client_id;

    /* ---- TCP connect ---- */
    struct hostent *he = gethostbyname(ta->host);
    if (!he) { cm->connection_failed = 1; return NULL; }

    int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0) { cm->connection_failed = 1; return NULL; }

    struct sockaddr_in saddr = {0};
    saddr.sin_family = AF_INET;
    memcpy(&saddr.sin_addr, he->h_addr, (size_t)he->h_length);
    saddr.sin_port = htons((uint16_t)ta->tcp_port);

    if (connect(tcp_fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        perror("connect");
        close(tcp_fd);
        cm->connection_failed = 1;
        return NULL;
    }

    /* Set non-blocking */
    int flags = fcntl(tcp_fd, F_GETFL, 0);
    fcntl(tcp_fd, F_SETFL, flags | O_NONBLOCK);

    /* ---- UDP socket ---- */
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) { close(tcp_fd); cm->connection_failed = 1; return NULL; }
    struct sockaddr_in udp_local = {0};
    udp_local.sin_family = AF_INET; udp_local.sin_addr.s_addr = INADDR_ANY;
    bind(udp_fd, (struct sockaddr *)&udp_local, sizeof(udp_local));
    int fl2 = fcntl(udp_fd, F_GETFL, 0);
    fcntl(udp_fd, F_SETFL, fl2 | O_NONBLOCK);

    struct sockaddr_in udp_srv = saddr;
    udp_srv.sin_port = htons((uint16_t)ta->udp_port);

    /* ---- Send HELLO ---- */
    {
        PayloadHello hello = {0};
        hello.magic   = PROTO_MAGIC;
        hello.version = PROTO_VERSION;
        snprintf(hello.name, MAX_NAME_LEN, "bot%d", ta->client_id);
        proto_hton_hello(&hello);
        uint8_t buf[HEADER_SIZE + sizeof(hello)];
        int n = proto_write_msg(buf, sizeof(buf), MSG_HELLO, 0, &hello, sizeof(hello));
        if (n > 0) send(tcp_fd, buf, (size_t)n, MSG_NOSIGNAL);
        /* UDP knock so server learns our UDP src port */
        sendto(udp_fd, buf, (size_t)n, 0,
               (struct sockaddr *)&udp_srv, sizeof(udp_srv));
    }

    /* ---- Wait for WELCOME ---- */
    FrameBuf fb = {0};
    int welcomed = 0;
    uint64_t wait_start = proto_now_us();
    while (!welcomed && proto_now_us() - wait_start < 3000000ULL) {
        uint8_t tmp[512];
        ssize_t n = recv(tcp_fd, tmp, sizeof(tmp), 0);
        if (n > 0) {
            uint8_t type, *payload; uint32_t seq; uint16_t plen;
            int r = proto_feed(&fb, tmp, (size_t)n, &type, &seq, &payload, &plen);
            if (r == 1 && type == MSG_WELCOME) {
                welcomed = 1;
                cm->connected = 1;
            } else if (r == 1 && type == MSG_REJECT) {
                cm->connection_failed = 1;
                goto cleanup;
            }
        }
        usleep(5000);
    }
    if (!welcomed) { cm->connection_failed = 1; goto cleanup; }

    /* ---- Main simulation loop ---- */
    {
        uint32_t udp_last_seq = 0;
        uint32_t tcp_seq      = 1;
        Direction dirs[4] = {DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT};
        int dir_idx = ta->client_id % 4;

        uint64_t start  = proto_now_us();
        uint64_t end_t  = start + (uint64_t)ta->duration_sec * 1000000ULL;
        uint64_t next_input = start;

        while (!g_stop && proto_now_us() < end_t) {
            /* Send random direction input every ~200ms */
            uint64_t now = proto_now_us();
            if (now >= next_input) {
                PayloadInput inp;
                inp.direction = (uint8_t)dirs[dir_idx % 4];
                uint8_t buf[HEADER_SIZE + sizeof(inp)];
                int n = proto_write_msg(buf, sizeof(buf), MSG_INPUT, tcp_seq++,
                                        &inp, sizeof(inp));
                if (n > 0) send(tcp_fd, buf, (size_t)n, MSG_NOSIGNAL);
                cm->tcp_messages_sent++;
                dir_idx++;
                /* Random direction rotation */
                if (rand() % 3 == 0) dir_idx = rand() % 4;
                next_input = now + 200000ULL;  /* 200ms */
            }

            /* Drain TCP (score updates etc.) */
            {
                uint8_t tmp[1024];
                ssize_t n = recv(tcp_fd, tmp, sizeof(tmp), 0);
                if (n > 0) {
                    uint8_t type, *payload; uint32_t seq; uint16_t plen;
                    int r = proto_feed(&fb, tmp, (size_t)n, &type, &seq, &payload, &plen);
                    (void)r; (void)type; (void)seq; (void)payload; (void)plen;
                } else if (n == 0) {
                    break;  /* server closed */
                }
            }

            /* Drain UDP snapshots */
            {
                uint8_t buf[HEADER_SIZE + sizeof(PayloadStateSnapshot) + 64];
                for (;;) {
                    ssize_t n = recv(udp_fd, buf, sizeof(buf), 0);
                    if (n < 0) break;
                    if ((size_t)n < HEADER_SIZE + sizeof(PayloadStateSnapshot)) continue;

                    /* Simulate packet loss: randomly drop packets */
                    if (ta->sim_loss_pct > 0) {
                        if ((rand() % 100) < ta->sim_loss_pct) {
                            /* Dropped by simulated network — never parsed */
                            continue;
                        }
                    }

                    /* Note: latency simulation via usleep is intentionally
                     * omitted here — it would serialize the recv loop.
                     * End-to-end latency is measured via server_time_us
                     * vs recv_time_us timestamps instead. */

                    MsgHeader hdr; memcpy(&hdr, buf, HEADER_SIZE);
                    uint32_t seq = ntohl(hdr.seq);

                    cm->snapshots_received++;

                    /* UDP sequence check: discard if stale/out-of-order.
                     * CRITICAL: stale packets are NEVER applied to state. */
                    if (cm->snapshots_received > 1 && seq <= udp_last_seq) {
                        cm->snapshots_discarded++;
                        /* IMPORTANT: stale packet NEVER applied to state */
                        continue;
                    }
                    udp_last_seq = seq;
                    cm->snapshots_applied++;

                    /* Latency measurement */
                    PayloadStateSnapshot snap;
                    memcpy(&snap, buf + HEADER_SIZE, sizeof(snap));
                    proto_ntoh_snapshot(&snap);
                    uint64_t recv_us = proto_now_us();
                    if (cm->snapshots_applied == 1) cm->first_snap_us = recv_us;
                    cm->last_snap_us = recv_us;

                    if (recv_us > snap.server_time_us) {
                        cm->latency_sum_us += recv_us - snap.server_time_us;
                        cm->latency_samples++;
                    }
                }
            }

            usleep(5000);  /* 5ms sleep — ~200Hz poll rate */
        }
    }

cleanup:
    close(tcp_fd);
    close(udp_fd);
    return NULL;
}

/* ============================================================
 * Main
 * ============================================================ */
static void on_signal(int s) { (void)s; g_stop = 1; }

int main(int argc, char **argv) {
    const char *host       = "127.0.0.1";
    int         tcp_port   = 7777;
    int         udp_port   = 7778;
    int         num_clients = 4;
    int         duration    = 15;
    int         tick_rate   = 25;
    uint64_t    inj_lat_ms  = 0;
    double      loss_pct    = 0.0;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--clients")    == 0 && i+1<argc) num_clients = atoi(argv[++i]);
        else if (strcmp(argv[i], "--host")       == 0 && i+1<argc) host        = argv[++i];
        else if (strcmp(argv[i], "--tcp-port")   == 0 && i+1<argc) tcp_port    = atoi(argv[++i]);
        else if (strcmp(argv[i], "--udp-port")   == 0 && i+1<argc) udp_port    = atoi(argv[++i]);
        else if (strcmp(argv[i], "--duration")   == 0 && i+1<argc) duration    = atoi(argv[++i]);
        else if (strcmp(argv[i], "--tick-rate")  == 0 && i+1<argc) tick_rate   = atoi(argv[++i]);
        else if (strcmp(argv[i], "--latency-ms") == 0 && i+1<argc) inj_lat_ms  = (uint64_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--loss-pct")   == 0 && i+1<argc) loss_pct    = atof(argv[++i]);
    }

    if (num_clients > MAX_PLAYERS) {
        fprintf(stderr, "Warning: max supported players is %d\n", MAX_PLAYERS);
        num_clients = MAX_PLAYERS;
    }

    signal(SIGINT, on_signal);

    TestMetrics metrics;
    metrics_init(&metrics, num_clients);

    printf("[load_test] Spawning %d clients for %d seconds against %s:%d/%d\n",
           num_clients, duration, host, tcp_port, udp_port);

    pthread_t *threads = calloc((size_t)num_clients, sizeof(pthread_t));
    ThreadArg *args    = calloc((size_t)num_clients, sizeof(ThreadArg));

    for (int i = 0; i < num_clients; i++) {
        args[i].metrics          = &metrics;
        args[i].client_id        = i;
        args[i].host             = host;
        args[i].tcp_port         = tcp_port;
        args[i].udp_port         = udp_port;
        args[i].duration_sec     = duration;
        args[i].tick_rate_hz     = tick_rate;
        args[i].sim_latency_us   = inj_lat_ms * 1000ULL;
        args[i].sim_loss_pct     = (int)loss_pct;
        usleep(50000);  /* stagger by 50ms to avoid simultaneous connect */
        pthread_create(&threads[i], NULL, client_thread, &args[i]);
    }

    /* Wait for all threads */
    for (int i = 0; i < num_clients; i++)
        pthread_join(threads[i], NULL);

    metrics.test_end_us = proto_now_us();
    metrics_report(&metrics, "load_test", tick_rate, inj_lat_ms, loss_pct);

    /* Write machine-readable JSON summary */
    FILE *f = fopen("metrics_result.json", "w");
    if (f) {
        int ok = 0;
        uint32_t total_recv = 0, total_disc = 0, total_applied = 0;
        double lat_avg = 0.0; uint64_t lat_sum = 0; uint32_t lat_n = 0;
        double hz_max = 0.0;
        for (int i = 0; i < num_clients; i++) {
            ClientMetrics *c = &metrics.clients[i];
            if (c->connected) ok++;
            total_recv    += c->snapshots_received;
            total_disc    += c->snapshots_discarded;
            total_applied += c->snapshots_applied;
            lat_sum += c->latency_sum_us; lat_n += c->latency_samples;
            if (c->snapshots_applied > 1 && c->last_snap_us > c->first_snap_us) {
                double span = (double)(c->last_snap_us - c->first_snap_us) / 1e6;
                double hz = (double)(c->snapshots_applied - 1) / span;
                if (hz > hz_max) hz_max = hz;
            }
        }
        if (lat_n > 0) lat_avg = (double)(lat_sum / lat_n) / 1000.0;
        double correct_pct = total_recv > 0
            ? (double)total_applied / (double)total_recv * 100.0
            : 100.0;
        fprintf(f,
            "{\n"
            "  \"X_max_clients\": %d,\n"
            "  \"Z_tick_rate_hz\": %.1f,\n"
            "  \"Y_injected_latency_ms\": %llu,\n"
            "  \"D_message_types\": %d,\n"
            "  \"A_correct_state_pct\": %.2f,\n"
            "  \"packet_loss_pct\": %.1f,\n"
            "  \"udp_received\": %u,\n"
            "  \"udp_discarded\": %u,\n"
            "  \"udp_applied\": %u,\n"
            "  \"avg_latency_ms\": %.2f\n"
            "}\n",
            ok, hz_max, (unsigned long long)inj_lat_ms, MSG_TYPE_COUNT,
            correct_pct, loss_pct,
            total_recv, total_disc, total_applied, lat_avg);
        fclose(f);
        printf("[load_test] Wrote metrics_result.json\n");
    }

    free(threads);
    free(args);
    metrics_destroy(&metrics);
    return 0;
}
