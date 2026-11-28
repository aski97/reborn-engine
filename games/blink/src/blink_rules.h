/*
 * Blink's rules (the simulation layer, ADR 0014): what a click does, how time
 * runs out, when the game ends. No window, no GPU, no clock: actions come in,
 * the state and the step's events come out, so the rules run the same in the
 * game, in tests and in a replay.
 *
 *   blink_start(&state, &blink_default_tuning, seed);
 *   blink_events events;
 *   blink_step(&state, actions, count, dt, &events);   // once per fixed step
 */
#ifndef BLINK_RULES_H
#define BLINK_RULES_H

#include <reborn/defines.h>
#include <reborn/math.h>
#include <reborn/random.h>

/* How many colors a square can take: the rules pick an index, the view owns the colors. */
#define BLINK_COLOR_COUNT 6u

/* Actions in one step: far more than a player can click in 1/60 s. */
#define BLINK_MAX_ACTIONS 16u

/* One per action at most, then a lost square and the game over: a step's events always fit. */
#define BLINK_MAX_EVENTS (BLINK_MAX_ACTIONS + 2u)

/* ============================================================================
 * Data
 * ========================================================================== */

/* Balance numbers, kept out of the logic: tests use their own, a file may set them later. */
typedef struct blink_tuning {
    rb_vec2 field;           /* the playing field in world units, origin top left */
    f32     square_side;     /* a new square's side, world units */
    f32     end_scale;       /* the side left as its time runs out, a share of square_side */
    f32     first_lifetime;  /* seconds the first square lasts */
    f32     lifetime_factor; /* each hit multiplies the lifetime by this: the speed-up */
    f32     min_lifetime;    /* the speed-up stops here */
    /*
     * Seconds a click looks back. The frame a player sees left the rules this
     * long ago, so a click is tested against the square as it was then, and a
     * square stays clickable this long after it vanishes from the rules' now.
     */
    f32 hit_grace;
    f32 ready_time; /* seconds of READY before a game's first square */
    u32 lives;
    f32 restart_delay; /* seconds after the game over before a click restarts */
} blink_tuning;

/*
 * 16x9 field, side 1.5 shrinking to 40%, 1.5 s down 5% per hit to 0.6 s, a
 * 50 ms grace, 0.6 s of READY, 3 lives, 1 s before a click restarts.
 */
extern const blink_tuning blink_default_tuning;

/* ============================================================================
 * State
 * ========================================================================== */

typedef enum blink_phase {
    BLINK_PHASE_TITLE, /* before the first game: a click or Space starts it */
    BLINK_PHASE_READY, /* a game has started, its first square not yet: clicks do nothing */
    BLINK_PHASE_PLAYING,
    BLINK_PHASE_GAME_OVER,
} blink_phase;

typedef struct blink_square {
    rb_vec2 center;
    f32     lifetime;  /* seconds it lasts in all */
    f32     remaining; /* seconds left: it shrinks with them; down to -hit_grace while playing */
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
    u32          best;       /* best score of the session */
    f32          phase_time; /* seconds in the current phase: READY, the restart delay, blinking */
} blink_state;

/* ============================================================================
 * Actions in, events out
 * ========================================================================== */

typedef enum blink_action_type {
    BLINK_ACTION_CLICK, /* `point`: where, in world units */
    BLINK_ACTION_START, /* a new game: from the title, or once the current one is over */
} blink_action_type;

typedef struct blink_action {
    blink_action_type type;
    rb_vec2           point;
} blink_action;

/* What happened in a step: the view reacts (a log line, an effect, a sound later). */
typedef enum blink_event_type {
    BLINK_EVENT_STARTED,       /* a new game began: READY, then its first square */
    BLINK_EVENT_HIT,           /* the square was clicked: a point */
    BLINK_EVENT_CLICK_OUTSIDE, /* a click while playing that missed the square: it costs nothing */
    BLINK_EVENT_EXPIRED,       /* the square's time ran out: a life lost */
    BLINK_EVENT_GAME_OVER,     /* the last life went */
} blink_event_type;

/* Each type fills the fields it names; the others stay zero. */
typedef struct blink_event {
    blink_event_type type;
    rb_vec2          point;    /* HIT, CLICK_OUTSIDE: the click, world units */
    rb_rect          square;   /* HIT, CLICK_OUTSIDE: as the player saw it; EXPIRED: its last */
    u32              color;    /* HIT, EXPIRED: the square's */
    f32              distance; /* CLICK_OUTSIDE: from the square the player saw, world units */
    bool             graced;   /* HIT: outside the square as it is now, inside as it was seen */
} blink_event;

/* A step's events in the order they happened: the actions first, then the time. */
typedef struct blink_events {
    blink_event items[BLINK_MAX_EVENTS];
    u32         count;
} blink_events;

/* ============================================================================
 * Functions
 * ========================================================================== */

/* A new session, on the title: the first game starts with a click or a start action. */
void blink_start(blink_state *state, const blink_tuning *tuning, u64 seed);

/*
 * One fixed step: the actions in order (at most BLINK_MAX_ACTIONS), then `dt`
 * seconds pass; `events` receives what happened. On the title, a click or a
 * start action starts the first game, which waits ready_time before its first
 * square. While playing, a click on the square (as seen hit_grace earlier)
 * scores and brings the next one, faster; a click elsewhere costs nothing.
 * After the game over, a start action, or a click once restart_delay has
 * passed, starts a new game.
 */
void blink_step(blink_state *state, const blink_action *actions, u32 count, f32 dt,
                blink_events *events);

/*
 * The square as it is now, shrunk around its center by the time left, down to
 * end_scale; empty once its time is up. What the view draws.
 */
rb_rect blink_square_rect(const blink_state *state);

#endif /* BLINK_RULES_H */
