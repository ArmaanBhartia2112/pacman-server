#ifndef METRICS_H
#define METRICS_H

#include <stdint.h>
#include <pthread.h>

typedef struct {
    int      client_id;
    int      connected;          /* 1 = successfully authenticated */
    int      connection_failed;  /* 1 = never authenticated */
    uint32_t snapshots_received; /* total UDP snapshots received */
    uint32_t snapshots_discarded;/* stale/out-of-order discarded */
    uint32_t snapshots_applied;  /* snapshots that updated state */
    uint64_t first_snap_us;      /* time of first snapshot */
    uint64_t last_snap_us;       /* time of last snapshot */
    uint32_t tcp_messages_sent;
    /* Latency: accumulated for averaging */
    uint64_t latency_sum_us;
    uint32_t latency_samples;
} ClientMetrics;

typedef struct {
    ClientMetrics *clients;
    int            count;
    pthread_mutex_t lock;
    /* Server instrumentation */
    uint64_t test_start_us;
    uint64_t test_end_us;
} TestMetrics;

void metrics_init(TestMetrics *m, int client_count);
void metrics_destroy(TestMetrics *m);
void metrics_report(const TestMetrics *m, const char *test_name,
                    int tick_rate_hz, uint64_t injected_latency_ms,
                    double packet_loss_pct);

#endif /* METRICS_H */
