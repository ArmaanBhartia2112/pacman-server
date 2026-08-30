#include "interp.h"
#include <string.h>

void interp_init(Interpolator *itp) {
    memset(itp, 0, sizeof(*itp));
}

void interp_push(Interpolator *itp, const PayloadStateSnapshot *snap,
                 uint64_t recv_time_us) {
    /* Shift: [1] becomes [0], new snap becomes [1] */
    itp->snaps[0]      = itp->snaps[1];
    itp->recv_time_us[0] = itp->recv_time_us[1];
    itp->snaps[1]      = *snap;
    itp->recv_time_us[1] = recv_time_us;
    if (itp->count < 2) itp->count++;
}

/* Linear interpolation between int16 values */
static int16_t lerp16(int16_t a, int16_t b, float t) {
    return (int16_t)((float)a + t * (float)(b - a));
}

void interp_get(const Interpolator *itp, uint64_t now_us,
                InterpPlayer out_players[MAX_PLAYERS], int *out_player_count,
                InterpGhost  out_ghosts[MAX_GHOSTS],  int *out_ghost_count) {
    if (itp->count == 0) {
        *out_player_count = 0;
        *out_ghost_count  = 0;
        return;
    }

    const PayloadStateSnapshot *newest = &itp->snaps[1];

    if (itp->count < 2) {
        /* Only one snapshot — just use it directly */
        *out_player_count = newest->player_count;
        *out_ghost_count  = newest->ghost_count;
        for (int i = 0; i < newest->player_count; i++) {
            const PlayerPos *pp = &newest->players[i];
            out_players[i].x         = pp->x;
            out_players[i].y         = pp->y;
            out_players[i].player_id = pp->player_id;
            out_players[i].direction = pp->direction;
            out_players[i].alive     = pp->alive;
        }
        for (int i = 0; i < newest->ghost_count; i++) {
            const GhostPos *gp = &newest->ghosts[i];
            out_ghosts[i].x        = gp->x;
            out_ghosts[i].y        = gp->y;
            out_ghosts[i].ghost_id = gp->ghost_id;
            out_ghosts[i].state    = gp->state;
        }
        return;
    }

    /* Compute alpha: how far between [0] and [1] based on receive times */
    uint64_t t0 = itp->recv_time_us[0];
    uint64_t t1 = itp->recv_time_us[1];
    float alpha = 0.0f;
    if (t1 > t0) {
        float span = (float)(t1 - t0);
        float elapsed = (float)(now_us > t0 ? now_us - t0 : 0);
        alpha = elapsed / span;
        if (alpha > 1.5f) alpha = 1.5f;  /* allow slight extrapolation */
    }

    const PayloadStateSnapshot *prev = &itp->snaps[0];

    /* Interpolate players */
    *out_player_count = newest->player_count;
    for (int i = 0; i < newest->player_count && i < MAX_PLAYERS; i++) {
        const PlayerPos *np = &newest->players[i];
        out_players[i].player_id = np->player_id;
        out_players[i].direction = np->direction;
        out_players[i].alive     = np->alive;

        /* Find matching player in prev snapshot */
        const PlayerPos *pp = NULL;
        for (int j = 0; j < prev->player_count; j++) {
            if (prev->players[j].player_id == np->player_id) {
                pp = &prev->players[j];
                break;
            }
        }
        if (pp) {
            out_players[i].x = lerp16(pp->x, np->x, alpha);
            out_players[i].y = lerp16(pp->y, np->y, alpha);
        } else {
            out_players[i].x = np->x;
            out_players[i].y = np->y;
        }
    }

    /* Interpolate ghosts */
    *out_ghost_count = newest->ghost_count;
    for (int i = 0; i < newest->ghost_count && i < MAX_GHOSTS; i++) {
        const GhostPos *ng = &newest->ghosts[i];
        out_ghosts[i].ghost_id = ng->ghost_id;
        out_ghosts[i].state    = ng->state;

        const GhostPos *pg = NULL;
        for (int j = 0; j < prev->ghost_count; j++) {
            if (prev->ghosts[j].ghost_id == ng->ghost_id) {
                pg = &prev->ghosts[j];
                break;
            }
        }
        if (pg) {
            out_ghosts[i].x = lerp16(pg->x, ng->x, alpha);
            out_ghosts[i].y = lerp16(pg->y, ng->y, alpha);
        } else {
            out_ghosts[i].x = ng->x;
            out_ghosts[i].y = ng->y;
        }
    }
}

uint64_t interp_latency_us(const Interpolator *itp) {
    if (itp->count == 0) return 0;
    const PayloadStateSnapshot *snap = &itp->snaps[1];
    uint64_t recv = itp->recv_time_us[1];
    /* Local recv time minus server_time_us gives round-trip estimate.
     * (Clocks may not be synchronized on different machines, but on loopback
     * this gives a meaningful number.) */
    if (recv > snap->server_time_us)
        return recv - snap->server_time_us;
    return 0;
}
