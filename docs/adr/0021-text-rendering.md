# 0021 — Text rendering

- **Status:** Accepted
- **Date:** 2026-11-24

## Context
Fonts give glyphs: an atlas rectangle, an offset from the pen and an advance (ADR 0018). Blink's
HUD (score, lives, game over) and its title screen need strings drawn on top of that: measured
before drawing (to center "GAME OVER"), aligned to a point (a score growing leftward from the
right edge), on several lines, at a size that makes sense in window pixels and in world units.

The builtin font is 8x8 pixels with nearest filtering: drawn at an integer multiple of its size,
each font pixel covers the same number of screen pixels; at a fraction (3.125) some columns get
one screen pixel more than others and strokes look uneven. Smooth fonts (TrueType, backlog) do
not have the problem.

Only the simplest layout is in scope: one glyph per character, left to right. Shaping, bidirectional
text, kerning and word wrapping are not.

## Options
**A. Size**
1. `size`: the height of a line (ascent + descent) in the units of the current space — 32 in
   pixels is the builtin font at x4, 0.5 in world units; means the same for any font.
2. `scale`: a multiple of the font's own pixels — natural in pixels, awkward in world units (scale
   1 is 8 units), meaningless for TrueType.

**B. API shape**
1. A reusable `rb_text_style` (font, size, color, alignment) passed by pointer to the draw and
   measure calls — a HUD defines a few styles and reuses them; omitted fields take their zero
   default; new fields do not change existing calls. One more type.
2. Every value as a parameter — 8 arguments repeated at each call.

**C. Alignment**
1. `(x, y)` is an anchor: horizontally left, center or right; vertically top, middle, baseline or
   bottom, measured on the font's metrics, not on the ink, so a line does not move when its
   characters change.
2. Top-left only, plus measuring: the caller computes every anchor.

**D. Lines**
1. `\n` starts a new line, `ascent + descent + line_gap` below; each line aligned on its own.
2. One line per call.

**E. Encoding**
1. UTF-8 decoded now; an invalid sequence is one replacement character (the missing glyph) — a
   character beyond ASCII is one box, and fonts with more glyphs later need no change here.
2. One byte per character — `é` shows as two boxes; the contract changes with UTF-8.

**F. Numbers in text**
1. The caller formats with `snprintf` into its own buffer — standard C, minimal API.
2. A printf-like `rb_text_drawf` with an internal buffer — convenient; a fixed size that truncates.

**G. HUD space** (a game's choice; the engine only has to allow it)
1. Window pixels at an integer scale from the window's height, `max(1, floor(height / 180))`: x4 at
   720p, x6 at 1080p, x8 at 1440p, x12 at 2160p — exact proportion on common screens (multiples of
   180), steps in between, always sharp; anchored to the window's edges.
2. A virtual resolution through a HUD camera (`rb_camera2d` 320x180) — exact proportion
   everywhere, letterboxed with the world; uneven bitmap text at fractional scales. No engine work.
3. 2 plus snapping in the engine: the size rounded so a font pixel covers whole screen pixels —
   sharp and almost proportional; a new renderer query and snapping math, for a problem only
   bitmap fonts have.

**H. Where the code lives**
1. `engine/src/text/` next to the fonts, public `reborn/text.h`: a layer on `reborn/font.h` and
   the public renderer; the layout (where each glyph goes) a pure function apart from the drawing,
   tested without a GPU.

## Decision
**A1, B1, C1, D1, E1, F1, G1 (in Blink), H1.**
- **API** (`reborn/text.h`): `rb_text_style {font, size, color, align_x, align_y}`;
  `rb_text_draw(renderer, x, y, style, text)` in the current space (`rb_renderer_set_camera`),
  only inside `render`; `rb_text_measure(style, text)` → `rb_vec2` (width of the widest line,
  height of the block), callable anywhere. Zero alignment is left and top. A size of 0 or less
  draws nothing and measures zero.
- **Metrics:** the factor from font pixels to the space is `size / (ascent + descent)`. A line's
  width is the sum of its advances; the block's height `n * (ascent + descent) + (n - 1) *
  line_gap`, times the factor, for `n` lines (the number of `\n` plus one: an empty string is one
  empty line).
- **Anchors:** top puts the block's top at `y`, middle its center, bottom its bottom, baseline the
  first line's baseline. Each line starts at `x`, `x - width / 2` or `x - width`.
- **UTF-8:** a decoder in `text/utf8.c`. Invalid input (a stray continuation byte, a truncated
  sequence, an overlong form, a surrogate, beyond U+10FFFF) gives U+FFFD and consumes the maximal
  invalid subpart, as Unicode recommends: one box per broken character. Only `\n` is special;
  other control characters are glyphs (the missing box for the builtin font).
- **Layout:** `rb_text_layout(style, x, y, text, emit, context)` calls `emit` with each visible
  glyph's rectangle in the space and in the atlas; `rb_text_draw` passes one that draws it. A font
  can be built without a GPU by tests (`rb_font_set_glyphs` in `text/font_internal.h`).
- **Sharpness** is the caller's business: an integer multiple of the font's height and whole-pixel
  anchors keep bitmap text sharp in window pixels. Blink computes its HUD scale (G1); the helper
  moves into the engine when a second game needs it.
- **No allocation:** text drawing reads the string and the font, nothing is kept.

## Consequences
- Blink's HUD and title screen, and the sandbox's text, go through one code path; `measure` lets
  games put panels behind text.
- Text in world units (labels over objects) works through the world's camera, sharp only at
  integer pixel ratios.
- A virtual-resolution UI makes sense later in two cases, both in the backlog: smooth TrueType
  fonts (a continuous scale from a reference resolution, with anchors: likely Hollow's menus), and
  a pixel-art game drawn whole at a low resolution into an offscreen image, then enlarged by an
  integer factor.
- One draw per glyph: a few hundred per frame are fine until the batched renderer.
- Word wrapping to a width, kerning and per-character colors are not supported.
