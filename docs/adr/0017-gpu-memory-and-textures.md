# 0017 — GPU memory and textures

- **Status:** Accepted
- **Date:** 2026-11-23

## Context
Quads take their color from push constants (ADR 0015, F1). Blink's text needs the fragment shader
to read an image (a font atlas), which push constants cannot carry (128 bytes guaranteed). That
brings three Vulkan pieces the renderer does not have yet:
- **Device memory:** `vkAllocateMemory` returns raw memory of one of the GPU's memory types
  (device-local, host-visible...). It is slow (a kernel call) and the number of live allocations is
  capped (`maxMemoryAllocationCount`, 4096 guaranteed, exactly that on many Windows drivers).
  Buffers and images are bound to it.
- **Upload:** fast device-local memory is often not visible to the CPU, and images are stored in
  the GPU's own tiling, so pixels go through a host-visible staging buffer and a GPU copy, with
  layout transitions around it.
- **Sampler and descriptors:** a sampler says how a texture is read (filtering, edges); a
  descriptor tells the shader which image and sampler a slot (`set = 0, binding = 0`) refers to.

Blink needs a handful of textures created at load time. No external libraries without approval.

## Options
**A. What this step exposes**
1. Internal only: a 1x1 white texture sampled by the quad shader — least code; the sandbox cannot
   show a real texture, the API waits for fonts.
2. A minimal public API: `rb_texture` handle, create from RGBA pixels, destroy, draw a textured
   quad; the sandbox draws a generated pattern — what fonts will build their atlas with.
3. A2 plus a source rectangle (UV) per draw — what an atlas needs; guessed before fonts need it.

**B. Device memory allocation**
1. One `vkAllocateMemory` per resource, behind an internal function — simple; enough for tens of
   resources, not thousands (allocation cap, slow calls).
2. Own block allocator: large blocks per memory type, sub-allocated (alignment,
   `bufferImageGranularity`) — what engines do at scale; much code for a need not there yet.
3. Vulkan Memory Allocator (AMD) — the industry standard; an external library, written in C++.

**C. Upload**
1. Blocking: a staging buffer and a one-time command buffer per upload, submit, wait on a fence,
   free — simple and correct; stalls briefly, fine at load time.
2. Persistent staging ring, copies recorded into the frame — no stalls; staging lifetime tied to
   frames in flight, needed only for streaming.

**D. Descriptor model**
1. Classic: a set layout with one combined image sampler, a pool, one set per texture, bound when
   the texture changes — the model every reference teaches; no optional features.
2. Bindless: one array of every texture, bound once per frame, the index in the push constants
   (descriptor indexing) — suits batching; optional features to check, subtler rules.
3. Push descriptors: written into the command buffer, no pool or sets — least code; an extension
   on Vulkan 1.3 (core only in 1.4).

**E. Sampler filtering**
1. Always nearest — sharp pixels; scaled smooth images look blocky.
2. Always linear — smooth; pixel art and bitmap text get blurred.
3. Chosen per texture at creation: two samplers made at startup.

**F. Pixel format**
1. `R8G8B8A8_SRGB`: pixels as images store them, decoded to linear when sampled (like
   `rb_color_srgb` and the sRGB swapchain, ADR 0013, P1); alpha stays linear (glyph coverage).
2. `R8G8B8A8_UNORM`: values used as they are — right for data, wrong for colors.
3. Format chosen per texture (single-channel atlases...) — a memory optimization for later.

**G. Destroying a texture still used by frames in flight**
1. `vkDeviceWaitIdle`, then destroy — correct and simple; a stall, fine for rare destructions.
2. A deferred destruction queue, emptied when the frames in flight are done — no stall; more code,
   needed with streaming.

## Decision
**A2, B1, C1, D1, E3, F1, G1.** B2, C2 and D2 go to the backlog with the batched renderer.
- **API** (`reborn/renderer.h`): `rb_texture` is a generational handle (ADR 0007) like
  `rb_window`; `RB_RENDERER_MAX_TEXTURES` (256) live at once.
  `rb_renderer_create_texture(renderer, &(rb_texture_desc){width, height, pixels, filter})`
  takes `width * height * 4` bytes, RGBA, sRGB, straight alpha, rows top to bottom; it returns a
  zero handle on failure (logged: empty or too large, no free slot, Vulkan error). It may be called
  at any time, inside `render` too. `rb_renderer_destroy_texture` ignores stale and zero handles;
  calling it inside `render` is a bug (`RB_ASSERT`): the frame being recorded may already use it.
  `rb_renderer_draw_texture(renderer, x, y, width, height, texture, tint)` draws the whole texture
  stretched over the rectangle, multiplied by `tint`; a zero or stale handle draws a plain quad of
  the tint color. Textures still alive at shutdown are destroyed by it.
- **One shader:** `quad.frag` returns `texture(tex, uv) * color`; `rb_renderer_draw_quad` draws
  with an internal 1x1 white texture, so its signature and output do not change. UV (0, 0) is the
  texture's first row, at the quad's top-left corner.
- **Memory:** `rb_vk_memory_find_type` picks the first memory type the resource allows with the
  wanted flags: device-local for images (any allowed type if none), host-visible and coherent for
  staging (no flush needed). One allocation per image and per staging buffer.
- **Upload:** a transient command pool and fence per upload; barriers UNDEFINED → TRANSFER_DST
  (copy) → SHADER_READ_ONLY (fragment shader reads); the CPU waits for the fence, then frees the
  staging buffer. The barrier's second scope covers every later submission on the queue.
- **Descriptors:** one set layout (binding 0, combined image sampler, fragment stage) in the
  pipeline layout next to the push constants; a pool of `RB_RENDERER_MAX_TEXTURES + 1` sets
  (the white texture's included) created with `FREE_DESCRIPTOR_SET_BIT`, one set per texture
  written once at creation. Each frame begins with no set bound; a draw binds its texture's set
  only when it differs from the last one bound.
- **Samplers:** nearest and linear, clamp to edge (no bleeding from the atlas neighbor or the
  opposite edge), no mipmaps, no anisotropy.

## Consequences
- Fonts can upload their atlas and draw glyphs as textured quads; images (PNG) will need only a
  decoder.
- A source rectangle per draw comes with fonts (A3 then): the push constants have room (96 bytes
  used of 128).
- Every upload waits for the GPU: loading many textures mid-game would stutter (C2 when streaming
  arrives); destroying one stalls the GPU (G2 with streaming).
- Over the allocation cap or with many small resources, B1 stops scaling: B2 is in the backlog.
- With straight alpha and linear filtering, transparent pixels' color bleeds into the edges of
  sprites (dark fringes): white glyphs do not show it, PNG sprites will (premultiplied alpha, to
  decide with image decoding).
- With one draw per quad, a set bind per texture change costs nothing measurable; the batched
  renderer will have to break batches on texture changes (atlases, or D2).
