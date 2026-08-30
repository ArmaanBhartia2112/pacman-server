#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * Wire format constants
 * ============================================================ */
#define PROTO_MAGIC        0x50AC  /* "PA" — sanity check in HELLO */
#define PROTO_VERSION      1
#define MAX_PLAYERS        8
#define MAX_GHOSTS         4
#define MAX_NAME_LEN       16
#define MAZE_COLS          28
#define MAZE_ROWS          31
#define MAX_PELLETS        (MAZE_COLS * MAZE_ROWS)
#define HEADER_SIZE        7   /* sizeof(MsgHeader) with packing */

/* ============================================================
 * Message types  (D = 8 distinct types)
 * ============================================================ */
typedef enum {
    MSG_HELLO          = 1,  /* TCP  C→S  auth request           */
    MSG_WELCOME        = 2,  /* TCP  S→C  assign ID + maze layout */
    MSG_REJECT         = 3,  /* TCP  S→C  auth rejected           */
    MSG_INPUT          = 4,  /* TCP  C→S  direction input         */
    MSG_PELLET_EATEN   = 5,  /* TCP  S→C  pellet consumed event   */
    MSG_SCORE_UPDATE   = 6,  /* TCP  S→C  score changed           */
    MSG_GAME_OVER      = 7,  /* TCP  S→C  game ended              */
    MSG_STATE_SNAPSHOT = 8,  /* UDP  S→C  full position snapshot  */
    MSG_TYPE_COUNT     = 8
} MessageType;

/* ============================================================
 * Packed message header  (7 bytes on wire)
 * ============================================================ */
typedef struct __attribute__((packed)) {
    uint8_t  type;      /* MessageType                          */
    uint16_t length;    /* payload bytes AFTER header           */
    uint32_t seq;       /* monotonic sequence (per-connection)  */
} MsgHeader;

/* ============================================================
 * Directions
 * ============================================================ */
typedef enum {
    DIR_NONE  = 0,
    DIR_UP    = 1,
    DIR_DOWN  = 2,
    DIR_LEFT  = 3,
    DIR_RIGHT = 4
} Direction;

/* ============================================================
 * Ghost states
 * ============================================================ */
typedef enum {
    GHOST_SCATTER    = 0,
    GHOST_CHASE      = 1,
    GHOST_FRIGHTENED = 2
} GhostState;

/* ============================================================
 * Maze tile types
 * ============================================================ */
typedef enum {
    TILE_WALL        = 0,
    TILE_PELLET      = 1,
    TILE_POWER       = 2,  /* power pellet */
    TILE_EMPTY       = 3,
    TILE_GHOST_HOUSE = 4
} TileType;

/* ============================================================
 * In-wire entity positions (fixed-point: 1/16th tile units)
 * ============================================================ */
typedef struct __attribute__((packed)) {
    uint8_t  player_id;
    int16_t  x;          /* 1/16 tile units, signed */
    int16_t  y;
    uint8_t  direction;  /* Direction enum */
    uint8_t  alive;      /* 1 = alive, 0 = dead */
} PlayerPos;

typedef struct __attribute__((packed)) {
    uint8_t  ghost_id;
    int16_t  x;
    int16_t  y;
    uint8_t  state;      /* GhostState enum */
} GhostPos;

/* ============================================================
 * Payload structs (all multi-byte fields: network byte order)
 * ============================================================ */

/* MSG_HELLO */
typedef struct __attribute__((packed)) {
    uint16_t magic;              /* PROTO_MAGIC                  */
    uint8_t  version;            /* PROTO_VERSION                */
    char     name[MAX_NAME_LEN]; /* null-terminated player name  */
} PayloadHello;

/* MSG_WELCOME */
typedef struct __attribute__((packed)) {
    uint8_t  player_id;
    uint8_t  max_players;
    uint8_t  maze[MAZE_ROWS][MAZE_COLS];  /* TileType values */
    uint16_t tick_rate_hz;  /* server tick rate */
} PayloadWelcome;

/* MSG_REJECT */
typedef struct __attribute__((packed)) {
    char reason[64];
} PayloadReject;

/* MSG_INPUT */
typedef struct __attribute__((packed)) {
    uint8_t direction;   /* Direction enum */
} PayloadInput;

/* MSG_PELLET_EATEN */
typedef struct __attribute__((packed)) {
    uint8_t  row;
    uint8_t  col;
    uint8_t  is_power;   /* 1 = power pellet */
    uint8_t  player_id;
} PayloadPelletEaten;

/* MSG_SCORE_UPDATE */
typedef struct __attribute__((packed)) {
    uint8_t  player_id;
    uint32_t score;
} PayloadScoreUpdate;

/* MSG_GAME_OVER */
typedef struct __attribute__((packed)) {
    uint8_t  winner_id;   /* 0xFF = no winner (all dead) */
    uint32_t scores[MAX_PLAYERS];
} PayloadGameOver;

/* MSG_STATE_SNAPSHOT (UDP) */
typedef struct __attribute__((packed)) {
    uint64_t server_time_us;          /* server monotonic µs  */
    uint8_t  player_count;
    uint8_t  ghost_count;
    PlayerPos players[MAX_PLAYERS];
    GhostPos  ghosts[MAX_GHOSTS];
} PayloadStateSnapshot;

/* ============================================================
 * Framing buffer (per TCP connection)
 * ============================================================ */
#define FRAME_BUF_SIZE 8192

typedef struct {
    uint8_t  buf[FRAME_BUF_SIZE];
    size_t   len;  /* bytes currently in buf */
} FrameBuf;

/* ============================================================
 * API
 * ============================================================ */

/* Serialize: write header + payload into dst (caller provides buffer).
 * Returns total bytes written, or -1 on error. */
int proto_write_msg(uint8_t *dst, size_t dst_cap,
                    uint8_t type, uint32_t seq,
                    const void *payload, uint16_t payload_len);

/* Feed newly received bytes into a FrameBuf.
 * out_type, out_seq, out_payload, out_payload_len are set when a
 * complete message has been assembled.  Returns:
 *  1  — complete message ready (out_* filled)
 *  0  — need more data
 * -1  — framing error (buf overflow etc.) */
int proto_feed(FrameBuf *fb,
               const uint8_t *data, size_t len,
               uint8_t *out_type, uint32_t *out_seq,
               uint8_t **out_payload, uint16_t *out_payload_len);

/* Byte-order helpers: convert payload fields to/from host order.
 * Call hton_* before sending, ntoh_* after receiving. */
void proto_hton_hello(PayloadHello *p);
void proto_ntoh_hello(PayloadHello *p);

void proto_hton_welcome(PayloadWelcome *p);
void proto_ntoh_welcome(PayloadWelcome *p);

void proto_hton_score_update(PayloadScoreUpdate *p);
void proto_ntoh_score_update(PayloadScoreUpdate *p);

void proto_hton_game_over(PayloadGameOver *p);
void proto_ntoh_game_over(PayloadGameOver *p);

void proto_hton_snapshot(PayloadStateSnapshot *p);
void proto_ntoh_snapshot(PayloadStateSnapshot *p);

void proto_hton_pellet_eaten(PayloadPelletEaten *p);
void proto_ntoh_pellet_eaten(PayloadPelletEaten *p);

/* Utility: monotonic time in microseconds */
uint64_t proto_now_us(void);

#endif /* PROTOCOL_H */
