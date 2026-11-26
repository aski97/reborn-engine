# 0024 — Screen capture and PNG writing

- **Status:** Accepted
- **Date:** 2026-11-26

## Context
An agent drives Blink through commands (ADR 0022, 0023) and reads its state as JSON, but cannot
see it: a square drawn in the wrong place or HUD text cut by the window's edge passes every state
check. Agents read images as PNG, not raw formats (BMP, PPM).

Constraints:
- The image lives in GPU memory, in a swapchain image. After the present it belongs to the
  presentation engine and must not be read: it has to be copied before, at the end of the frame.
- Swapchain images allow only the uses requested at creation (`COLOR_ATTACHMENT` today); a copy
  needs `TRANSFER_SRC`, which drivers may not offer (nearly all desktop drivers do).
- Commands run at the start of a frame, before its steps and its render, and reply at once.
- A 1280x720 frame is 2.7 MB as raw RGB; image tools for agents cap file sizes. Blink's frames are
  mostly flat color.
- No external libraries without approval; this is a learning project.
- More encoders may come for other uses (other image formats, asset packing, save files): the
  pieces should be reusable without an interface built ahead of a second user.

## Options
**A. Which frame, and when the reply comes**
1. The command renders a frame itself (begin, the game's render, end with a capture), as `step`
   runs steps inside the command, and replies at once — the image shows the state at the command,
   whatever the pause; one extra frame is presented.
2. The command books a capture of the frame's own render and replies later — needs deferred
   replies; without a pause, more steps run before the image is taken.
3. Every frame copied to a buffer, the last one always ready — a cost on every frame for a rare
   event.

**B. Where the pixels are read**
1. The swapchain image, created with `TRANSFER_SRC` when the surface supports it; otherwise
   captures fail with an error. Small change.
2. Always draw into an offscreen image, then copy it to the swapchain — general (rendering
   without a window), but changes the whole frame path for a debugging feature.

**C. PNG encoder**
1. Own, DEFLATE stored blocks only (no compression) — simplest; megabytes per capture.
2. Own, LZ77 over a hash chain with fixed Huffman codes, a filter chosen per row — small files
   for flat frames, a few hundred lines.
3. Own, with dynamic Huffman codes — smaller still, twice the code.
4. `stb_image_write.h` — an external library.

**D. How the encoder is split**
1. One PNG file doing everything.
2. Layers by concern, each usable alone: checksums (CRC-32, Adler-32), DEFLATE and its zlib
   wrapper (any byte stream), the PNG container on top. No generic encoder interface until a
   second encoder exists.
3. A generic encoder interface now (a table of formats).

## Decision
**A1, B1, C2, D2.**

- **Layers (D2):**
  - `core/checksum.c`: `rb_crc32` and `rb_adler32`, each continuing from a previous value, so data
    can be fed in pieces. CRC-32 also serves future file formats, Adler-32 any zlib stream.
  - `compression/deflate.c`: raw DEFLATE (RFC 1951), one fixed-Huffman block; LZ77 over a 32 KiB
    window, a 3-byte hash with chains of at most 32 candidates. `rb_deflate_bound` gives the
    output size no input can exceed, so the caller allocates once. Scratch from a given allocator.
  - `compression/zlib.c`: the zlib wrapper (RFC 1950): 2-byte header, DEFLATE, Adler-32.
  - `image/png_write.c`: the PNG container (chunks with CRC, `IHDR`, one `IDAT`, `IEND`); 8-bit
    RGB when every pixel is opaque, RGBA otherwise; per row, the filter (None, Sub, Up, Average,
    Paeth) with the smallest sum of absolute values. Public `rb_image_write_png` in
    `<reborn/image.h>` (encode to memory, then one file write with stdio); a later format adds
    its own `rb_image_write_<format>` next to it.
  - Tests decode with a test-only inflate (stored and fixed-Huffman blocks, `tests/support/`),
    the seed of the PNG decoder the backlog needs for textures.
  - New memory tag `RB_MEMORY_TAG_IMAGE` for encoding scratch and capture pixels.
- **Renderer (B1):** the swapchain asks for `TRANSFER_SRC` when `supportedUsageFlags` has it.
  `rb_renderer_capture_frame` (inside a frame) books the frame being drawn: its end transitions
  the image for a copy, copies it into a host-visible buffer created for the capture, transitions
  it for the present, submits and presents as usual, then waits for the frame's fence and converts
  the pixels to `rb_image` RGBA (B8G8R8A8 and R8G8B8A8, sRGB or UNORM; alpha forced to 255: a
  window's alpha means nothing). `rb_renderer_captured_image` returns them, owned by the
  renderer, until the next capture; NULL when the last capture failed (logged).
- **Command (A1):** `screen.capture [path]` (the `screen` group of ADR 0023): begin a frame, the
  game's render with `alpha` 0 (the state exactly as the last step left it), capture, end, write
  the PNG. Default path `screenshot-<frame>.png` in the working directory. Reply: `path`,
  `width`, `height`, `bytes`, `frame`, `step`. Errors: nothing to show (minimized, no swapchain),
  capture unavailable or failed, file not written.
- **Games** use the same pieces: `rb_renderer_capture_frame` in their render, then
  `rb_renderer_captured_image` and `rb_image_write_png` (the sandbox's P key does, and shows the
  capture as a thumbnail texture).

## Consequences
- An agent sees what the player sees, at a known step, on every platform and under Xvfb.
- Golden-image tests can build on captures and a PNG decoder.
- A capture stalls the frame (fence wait, encoding on the main thread): fine for debugging and
  occasional screenshots, not for recording video.
- A driver without `TRANSFER_SRC` on swapchain images gets no captures until the offscreen path
  (B2) exists.
- Captured pixels are the swapchain's: at the window's pixel size, after sRGB encoding.
- The command takes any path the agent gives: the server listens on 127.0.0.1 only (ADR 0022).
