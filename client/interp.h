#ifndef INTERP_H
#define INTERP_H

#include "../protocol/protocol.h"
#include <stdint.h>

/* Holds the two most recent state snapshots for interpolation */
typedef struct {
    PayloadStateSnapshot snaps[2];  /* [0]=older, [1]=newer */
    uint64_t             recv_time_us[2];  /* local time when each arrived */
    int                  count;     /* 0, 1, or 2 snapshots received */
} Interpolator;

void interp_init(Interpolator *itp);

/* Push a new snapshot (call on each UDP snapshot received) */
void interp_push(Interpolator *itp, const PayloadStateSnapshot *snap,
                 uint64_t recv_time_us);

/* Compute interpolated positions at current time.
 * out_players and out_ghosts are filled with lerped positions.
 * player_count / ghost_count come from the latest snapshot. */
typedef struct {
    int16_t x, y;
    uint8_t player_id;
    uint8_t direction;
    uint8_t alive;
} InterpPlayer;

typedef struct {
    int16_t x, y;
    uint8_t ghost_id;
    uint8_t state;
} InterpGhost;

void interp_get(const Interpolator *itp, uint64_t now_us,
                InterpPlayer out_players[MAX_PLAYERS], int *out_player_count,
                InterpGhost  out_ghosts[MAX_GHOSTS],  int *out_ghost_count);

/* End-to-end latency estimate: difference between server_time and local recv */
uint64_t interp_latency_us(const Interpolator *itp);

#endif /* INTERP_H */
