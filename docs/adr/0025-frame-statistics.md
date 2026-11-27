# 0025 — Frame statistics and the stats overlay

- **Status:** Accepted
- **Date:** 2026-11-27

## Context
The sandbox draws a frame-time graph (toggled with L): the shape of the frame times, no values.
Frames per second, memory, validation counts and the GPU in use are only in the log: a line per
second, a memory report at exit, the GPU at startup. Text can now be drawn, so the overlay can show
numbers.

Constraints:
- With vsync, the interval between frames is the refresh period whatever the load: it says
  nothing about the time left. Only the time spent working does, and the waits (the fence and the
  swapchain image in `rb_renderer_begin_frame`, the frame pacer's sleep) happen inside the engine,
  out of the game's sight.
- An average hides stutter: 59 frames of 16.4 ms and one of 33 ms average 16.7 ms.
- Numbers redrawn every frame change 60 times a second and cannot be read.
- Performance is measured in release builds: debug code is several times slower.
- The renderer is opaque: the GPU, the present mode and the swapchain are not visible to games.
- Memory tags (ADR 0012) count the bytes asked for, whichever allocator serves them. What is
  allocated beyond that belongs to the allocators (arena pages committed, malloc overhead) and to
  what the engine does not track at all (the Vulkan driver, the C runtime, the code).
- No game needs an overlay yet; agents read state through commands (ADR 0022).

## Options
**A. Who measures, who draws**
1. The sandbox measures and draws — no engine API; no work time, the waits are out of its sight.
2. The engine measures and draws an overlay any game can turn on — every game gets it; needs a
   draw pass after the game's render, a font of its own, a key or command, a layout fit for any
   game; built before a game asks, and to be redone on the debug UI later.
3. The engine measures and publishes the numbers, the sandbox draws them — the numbers come from
   the loop itself, commands can report them; option 2 can later be built on top.

**B. How the frame numbers are summarized**
1. A one-second window, published once a second: frames per second, average and worst interval —
   readable, in line with the log.
2. Recomputed every frame over the graph's last 120 frames — current, unreadable.
3. An exponential moving average — smooth, hides spikes.
4. As 1, plus the 99th percentile of the interval (the "1% low" of benchmarking tools) and the
   time spent working, as a whole and split into update and render — percentiles say how often a
   game stutters, not only that it did once.

**C. GPU, present mode, swapchain**
1. One struct, `rb_renderer_info`, read with `rb_renderer_get_info` — the validation stats'
   pattern.
2. One getter per value.

**D. A command for agents** — the same data as JSON, or the overlay only.

**M. Memory shown**
1. Per tag: used and peak; per arena: used, peak, committed.
2. As 1, plus the process's resident memory (RAM in use, peak) from the OS — the whole picture,
   the driver included.
3. As 2, plus GPU memory (our device allocations, the heap budget with `VK_EXT_memory_budget`).

**E. Builds** — the statistics in every build, or in debug builds only.

## Decision
**A3, B4, C1, D (commands), M2, E: every build.**

- **Frame statistics (A3, B4).** `app/frame_stats.c`, pure arithmetic on times passed in (tested
  without a clock). Per frame the loop records the interval since the previous frame's start, the
  time spent waiting (`rb_renderer_begin_frame` and the pacer's sleep), the steps (pump, events,
  update) and the game's render. Work is the interval minus the waits: commands, steps, render,
  submit and present. Every second the window closes and `rb_app_get_frame_stats` returns:
  frames, steps and their rates; the interval's average, 99th percentile and maximum; average and
  maximum of work, update and render. Zero before the first window closes.
- **Percentile.** A histogram of the intervals, 0.1 ms buckets up to 100 ms and one for longer:
  constant memory, nothing sorted, 0.1 ms precision. The 99th percentile is the bucket where the
  frames counted from the shortest reach 99% (at 60 frames, the worst one), capped by the exact
  maximum; longer than 100 ms, the maximum.
- **Renderer info (C1).** `rb_renderer_info`: GPU name and type, present mode in use, vsync as
  requested, swapchain size and image count (0 without a swapchain). Read from the current state:
  a resize shows at once.
- **Process memory (M2).** `rb_process_get_memory`: resident bytes and their peak (Win32
  `GetProcessMemoryInfo`, working set; Linux `/proc/self/status`, `VmRSS` and `VmHWM`). False when
  the OS does not tell. GPU memory waits for a game with real textures (backlog).
- **Commands (D).** `stats` (the frame statistics), `memory.stats` (tags, arenas, process),
  `renderer.info` (GPU, swapchain, validation).
- **Every build (E).** A few clock reads and a counter per frame: a millionth of a 16.7 ms frame.
  The tag counters are always on already (ADR 0012). Detailed instrumentation (timed scopes,
  per-allocation call sites) is a profiler's, and can be compiled out then.
- **The overlay stays in the sandbox (A3).** Above the frame-time graph, toggled with it (L). An
  engine overlay (A2) comes with the debug UI or with the first game that asks.

## Consequences
- Games and agents read the same numbers; the sandbox shows them, and its per-second log line
  uses them.
- The work time includes submit and present: a driver that blocks in `vkQueuePresentKHR` instead
  of in acquire shows its wait as work.
- The window is a second: a stutter stays on screen for a second, and so does a recovery.
- The overlay covers part of the sandbox in a window smaller than about 1600x900 pixels; L hides it.
- `rb_app` grows by the histogram (4 KiB).
