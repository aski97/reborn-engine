# 0018 — Bitmap font and glyph atlas

- **Status:** Accepted
- **Date:** 2026-11-24

## Context
Blink shows its score and lives as text. The GPU draws only textured quads (ADR 0017), so text is
one quad per character, each showing that character's image (its **glyph**) taken from one texture
holding all of them (an **atlas**). Placing glyphs needs their **metrics**: where the image sits
relative to the pen on the baseline, and how far the pen moves after it (the advance).

Glyph images come either from a bitmap font (pixels drawn by hand, one size) or from an outline
font (TrueType: curves rasterized at any size). Blink needs a few digits and capitals at one size,
so a bitmap font is enough; an own TrueType rasterizer goes to the backlog for the first game with
text at several sizes. The code must make that later step a new glyph source, not a rewrite.

Today `rb_renderer_draw_texture` stretches the whole texture over the quad; an atlas needs a
source rectangle per draw (ADR 0017, A3). No file system layer exists yet. No external libraries.

## Options
**A. Source rectangle in the renderer**
1. `rb_renderer_draw_texture_region(..., texture, rb_rect source, tint)`, `source` in texture
   pixels; the renderer divides by the texture's size. A new `rb_rect` in `reborn/math.h`.
2. The same with `source` in UV (0..1) — the caller must know the texture's size.
3. A draw descriptor struct with optional fields (source, tint, later rotation) — extensible;
   changes the API's style for a need not there yet.

**B. Font data model**
1. General metrics per glyph (atlas rectangle, offset from the pen, advance) and per font (ascent,
   descent, line gap); the bitmap font fills them with equal values — TrueType fits as is.
2. Monospace only (a cell size) — simplest; TrueType would change the API and the text code.
3. A function table per font kind — every kind ends as atlas + metrics anyway.

**C. Atlas construction**
1. A generic packer: glyph images of any size placed on shelves (left to right, a new row when
   full), 1 pixel of padding between them so linear filtering never reads a neighbor — reused by
   TrueType.
2. A fixed grid for the bitmap font — less code now, rewritten for TrueType.

**D. Glyph lookup**
1. A fixed table for ASCII 32–126 plus a "missing" glyph, read only through `rb_font_get_glyph` —
   UTF-8 later changes the storage, not the callers.
2. A table over any codepoint — needs a hash map, which does not exist.

**E. The glyphs**
1. An 8x8 font of our own, written in the source as rows of `.` and `#` — readable, editable, no
   license.
2. `font8x8` (public domain) — ready; hex bytes, origin less clear.
3. 8x16 cells — nicer letters, twice the drawing.

**F. Atlas format**
1. RGBA: white, coverage in alpha — the existing texture path; the tint colors the text.
2. Single channel (R8) — a quarter of the memory; a new format and a shader change.

**G. Ownership**
1. Opaque `rb_font`, created from an allocator and destroyed by the caller, like the platform and
   the renderer — the layout can grow without touching games.
2. A public struct the game embeds — no allocation; its layout becomes API.

**H. Where the code lives**
1. `engine/src/text/` (fonts now, text drawing next) and `reborn/font.h`: a layer on the public
   renderer API; packer and glyph decoding are pure functions, tested without a GPU.
2. Inside `engine/src/renderer/` — but it is not backend code.

## Decision
**A1, B1, C1, D1, E1, F1, G1, H1.** The TrueType rasterizer (own code) goes to the backlog.
- **Three layers:** a glyph **source** produces, per glyph, a coverage image (one byte per pixel,
  0–255, rows from the top) and its metrics; the **packer** places the images in an atlas and
  fills its RGBA pixels; the **font** uploads the atlas and keeps the glyph table. Only the source
  is specific to the bitmap font: TrueType adds `rb_font_create_ttf` and a rasterizer.
- **Renderer:** `rb_renderer_draw_texture_region(renderer, x, y, width, height, texture, source,
  tint)`; `source` in texture pixels, origin at the top left; outside the texture the edge pixels
  repeat (clamp to edge). `rb_renderer_draw_texture` draws the region covering the whole texture.
  The push constants gain the UV offset and size (112 bytes of 128).
- **API** (`reborn/font.h`): `rb_font_create_builtin(allocator, renderer)` → `rb_font *` (NULL on
  failure, logged), `rb_font_destroy` (not inside `render`: it destroys the atlas texture),
  `rb_font_get_atlas`, `rb_font_get_glyph(font, codepoint)` → `rb_glyph {source, offset,
  advance}`, `rb_font_get_metrics` → `{ascent, descent, line_gap}`. Values in pixels at the font's own size: text
  drawing scales them. A codepoint outside the table gives the missing glyph (an empty box).
- **Builtin font:** 8x8 cells, nearest filtering; capitals and digits in rows 0–6, baseline under
  row 6 (ascent 7, descent 1), line gap 2, advance 8. Each glyph image is the whole cell, its offset
  from the pen (0, -7); a glyph without ink (the space) has no image and takes no atlas space.
  Column 7 stays empty: the gap between neighbors.
- **Packer:** images in table order, rows as tall as their tallest image, a fixed atlas width
  chosen by the source (128 for the builtin font), height as used. Fails when an image is wider
  than the atlas.
- **Memory:** the font and the temporary atlas pixels come from the caller's allocator, under a new
  tag `RB_MEMORY_TAG_TEXT`.

## Consequences
- Text drawing (the next step) needs only `rb_font_get_glyph` and the metrics, whatever the source.
- The builtin font is sharp only at integer scales (x2, x3); other scales or smooth text wait for
  the TrueType source.
- ASCII only: UTF-8 needs a codepoint → glyph map behind `rb_font_get_glyph`.
- The packer does not sort by height: fine for equal cells, wasteful for TrueType's varied glyphs
  (sorting is a local change in the packer).
