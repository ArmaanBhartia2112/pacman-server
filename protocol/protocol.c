#include "protocol.h"

#include <string.h>
#include <arpa/inet.h>  /* htons, htonl, ntohs, ntohl */
#include <time.h>
#include <stdio.h>

/* ============================================================
 * Monotonic clock helper
 * ============================================================ */
uint64_t proto_now_us(void) {
    struct timespec ts;
#ifdef __APPLE__
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

/* ============================================================
 * Serialize a message into dst
 * ============================================================ */
int proto_write_msg(uint8_t *dst, size_t dst_cap,
                    uint8_t type, uint32_t seq,
                    const void *payload, uint16_t payload_len) {
    size_t total = HEADER_SIZE + payload_len;
    if (total > dst_cap) return -1;

    MsgHeader hdr;
    hdr.type   = type;
    hdr.length = htons(payload_len);
    hdr.seq    = htonl(seq);

    memcpy(dst, &hdr, HEADER_SIZE);
    if (payload && payload_len > 0)
        memcpy(dst + HEADER_SIZE, payload, payload_len);
    return (int)total;
}

/* ============================================================
 * TCP framing: accumulate bytes, dispatch complete messages
 * Returns:
 *   1  — message ready   (out_* filled; out_payload points into fb->buf)
 *   0  — need more data
 *  -1  — framing error
 * ============================================================ */
int proto_feed(FrameBuf *fb,
               const uint8_t *data, size_t len,
               uint8_t *out_type, uint32_t *out_seq,
               uint8_t **out_payload, uint16_t *out_payload_len) {
    /* Append new data */
    if (fb->len + len > FRAME_BUF_SIZE) {
        /* overflow — reset and signal error */
        fb->len = 0;
        return -1;
    }
    memcpy(fb->buf + fb->len, data, len);
    fb->len += len;

    /* Check if we have a full header */
    if (fb->len < HEADER_SIZE) return 0;

    /* Peek at the header */
    MsgHeader hdr;
    memcpy(&hdr, fb->buf, HEADER_SIZE);
    hdr.length = ntohs(hdr.length);
    hdr.seq    = ntohl(hdr.seq);

    size_t total = HEADER_SIZE + hdr.length;
    if (fb->len < total) return 0;  /* payload not yet fully arrived */

    /* We have a complete message — fill out params */
    *out_type        = hdr.type;
    *out_seq         = hdr.seq;
    *out_payload     = fb->buf + HEADER_SIZE;
    *out_payload_len = hdr.length;

    /* Shift remaining bytes to the front of the buffer */
    size_t remainder = fb->len - total;
    if (remainder > 0)
        memmove(fb->buf, fb->buf + total, remainder);
    fb->len = remainder;

    return 1;
}

/* ============================================================
 * Byte-order conversions  (hton = host→network, ntoh = network→host)
 * ============================================================ */

void proto_hton_hello(PayloadHello *p) {
    p->magic = htons(p->magic);
    /* version, name: no conversion needed */
}
void proto_ntoh_hello(PayloadHello *p) {
    p->magic = ntohs(p->magic);
}

void proto_hton_welcome(PayloadWelcome *p) {
    p->tick_rate_hz = htons(p->tick_rate_hz);
    /* maze bytes: no conversion */
}
void proto_ntoh_welcome(PayloadWelcome *p) {
    p->tick_rate_hz = ntohs(p->tick_rate_hz);
}

void proto_hton_score_update(PayloadScoreUpdate *p) {
    p->score = htonl(p->score);
}
void proto_ntoh_score_update(PayloadScoreUpdate *p) {
    p->score = ntohl(p->score);
}

void proto_hton_game_over(PayloadGameOver *p) {
    for (int i = 0; i < MAX_PLAYERS; i++)
        p->scores[i] = htonl(p->scores[i]);
}
void proto_ntoh_game_over(PayloadGameOver *p) {
    for (int i = 0; i < MAX_PLAYERS; i++)
        p->scores[i] = ntohl(p->scores[i]);
}

/* PlayerPos and GhostPos contain int16_t fields — convert in-place */
static void hton_player(PlayerPos *p) {
    p->x = (int16_t)htons((uint16_t)p->x);
    p->y = (int16_t)htons((uint16_t)p->y);
}
static void ntoh_player(PlayerPos *p) {
    p->x = (int16_t)ntohs((uint16_t)p->x);
    p->y = (int16_t)ntohs((uint16_t)p->y);
}
static void hton_ghost(GhostPos *g) {
    g->x = (int16_t)htons((uint16_t)g->x);
    g->y = (int16_t)htons((uint16_t)g->y);
}
static void ntoh_ghost(GhostPos *g) {
    g->x = (int16_t)ntohs((uint16_t)g->x);
    g->y = (int16_t)ntohs((uint16_t)g->y);
}

void proto_hton_snapshot(PayloadStateSnapshot *p) {
    /* server_time_us: 64-bit — manual hton64 */
    uint64_t t = p->server_time_us;
    p->server_time_us = ((uint64_t)htonl((uint32_t)(t >> 32)) << 32) |
                        (uint64_t)htonl((uint32_t)(t & 0xFFFFFFFFULL));
    for (int i = 0; i < p->player_count && i < MAX_PLAYERS; i++)
        hton_player(&p->players[i]);
    for (int i = 0; i < p->ghost_count && i < MAX_GHOSTS; i++)
        hton_ghost(&p->ghosts[i]);
}
void proto_ntoh_snapshot(PayloadStateSnapshot *p) {
    uint64_t t = p->server_time_us;
    p->server_time_us = ((uint64_t)ntohl((uint32_t)(t >> 32)) << 32) |
                        (uint64_t)ntohl((uint32_t)(t & 0xFFFFFFFFULL));
    for (int i = 0; i < p->player_count && i < MAX_PLAYERS; i++)
        ntoh_player(&p->players[i]);
    for (int i = 0; i < p->ghost_count && i < MAX_GHOSTS; i++)
        ntoh_ghost(&p->ghosts[i]);
}

void proto_hton_pellet_eaten(PayloadPelletEaten *p) {
    (void)p; /* all uint8_t — no conversion needed */
}
void proto_ntoh_pellet_eaten(PayloadPelletEaten *p) {
    (void)p;
}
