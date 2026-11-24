/*
 * Blink's rules (the simulation layer, ADR 0014): what a click does, how time
 * runs out, when the game ends. No window, no GPU, no clock: actions come in,
 * the state and the step's events come out, so the rules run the same in the
 * game, in tests and in a replay.
 *
 *   blink_start(&state, &blink_default_tuning, seed);
 *   blink_events events = blink_step(&state, actions, count, dt);   // once per fixed step
 */
#ifndef BLINK_RULES_H
#define BLINK_RULES_H

#include <reborn/defines.h>
#include <reborn/math.h>
#include <reborn/random.h>

/* How many colors a square can take: the rules pick an index, the view owns the colors. */
#define BLINK_COLOR_COUNT 6u

/* ============================================================================
 * Data
 * ========================================================================== */

/* Balance numbers, kept out of the logic: tests use their own, a file may set them later. */
typedef struct blink_tuning {
    rb_vec2 field;           /* the playing field in world units, origin top left */
    f32     square_side;     /* a new square's side, world units */
    f32     first_lifetime;  /* seconds the first square lasts */
    f32     lifetime_factor; /* each hit multiplies the lifetime by this: the speed-up */
    f32     min_lifetime;    /* the speed-up stops here */
    u32     lives;
    f32     restart_delay; /* seconds after the game over before a click restarts */
} blink_tuning;

/* 16x9 field, side 1.5, 1.5 s down 5% per hit to 0.4 s, 3 lives, 1 s before a click restarts. */
extern const blink_tuning blink_default_tuning;

/* ============================================================================
 * State
 * ========================================================================== */

typedef enum blink_phase {
    BLINK_PHASE_PLAYING,
    BLINK_PHASE_GAME_OVER,
} blink_phase;

typedef struct blink_square {
    rb_vec2 center;
    f32     lifetime;  /* seconds it lasts in all */
    f32     remaining; /* seconds left: it shrinks with them */
    u32     color;     /* below BLINK_COLOR_COUNT */
} blink_square;

/* Read it freely; change it only through blink_start and blink_step. */
typedef struct blink_state {
    blink_tuning tuning;
    /*
     * Positions and colors. One generator for the whole session, restarts
     * included: the seed logged at startup repeats every game played.
     */
    rb_rng       rng;
    blink_phase  phase;
    blink_square square;   /* meaningful while playing */
    f32          lifetime; /* what the next square gets */
    u32          score;
    u32          lives;
    u32          best;      /* best score of the session */
    f32          over_time; /* seconds since the game over */
} blink_state;

/* ============================================================================
 * Actions in, events out
 * ========================================================================== */

typedef enum blink_action_type {
    BLINK_ACTION_CLICK,   /* `point`: where, in world units */
    BLINK_ACTION_RESTART, /* a new game, once the current one is over */
} blink_action_type;

typedef struct blink_action {
    blink_action_type type;
    rb_vec2           point;
} blink_action;

/* What happened in a step, as bits: the view reacts (a log line today, a sound later). */
typedef enum blink_event {
    BLINK_EVENT_STARTED = 1u << 0,   /* a new game began */
    BLINK_EVENT_HIT = 1u << 1,       /* the square was clicked: a point */
    BLINK_EVENT_EXPIRED = 1u << 2,   /* time ran out: a life lost */
    BLINK_EVENT_GAME_OVER = 1u << 3, /* the last life went */
} blink_event;

typedef u32 blink_events;

/* ============================================================================
 * Functions
 * ========================================================================== */

/* A new session: the first game starts at once, its first square already placed. */
void blink_start(blink_state *state, const blink_tuning *tuning, u64 seed);

/*
 * One fixed step: the actions in order, then `dt` seconds pass. While playing,
 * a click inside the square scores and brings the next one, faster; a click
 * elsewhere does nothing. After the game over, a restart action, or a click
 * once restart_delay has passed, starts a new game.
 */
blink_events blink_step(blink_state *state, const blink_action *actions, u32 count, f32 dt);

/*
 * The square as it is now, shrunk around its center by the time left: what
 * the view draws and what a click must hit.
 */
rb_rect blink_square_rect(const blink_state *state);

#endif /* BLINK_RULES_H */
