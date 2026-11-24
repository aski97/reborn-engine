# 0020 — Window icon

- **Status:** Accepted
- **Date:** 2026-11-24

## Context
Blink's window should show its own icon in the title bar, Alt+Tab and the taskbar (or the window
manager's panel on Linux) instead of the system's default. The system draws it, not the renderer,
so it belongs to the platform layer, like the title.

One icon is shown at several sizes: 16x16 in a title bar at 100% scale (20, 24, 32 at higher
scales), 32x32 and more in Alt+Tab and the taskbar. A single image scaled by the system blurs when
enlarged and loses detail when shrunk, so icon formats carry one image per size and the system
picks one.
- **Win32:** an `HICON` made from pixels (`CreateIconIndirect`), set with `WM_SETICON` twice: small
  (title bar) and big (Alt+Tab, taskbar), at the sizes `GetSystemMetricsForDpi` gives for the
  window's DPI. An `HICON` must be destroyed (`DestroyIcon`).
- **X11:** the `_NET_WM_ICON` property, a list of 32-bit values: width, height, then pixels as
  `0xAARRGGBB`, for every size one after the other; the window manager picks. Xlib takes values of
  format 32 as an array of `long`, 64 bits on 64-bit Linux.

No file system or image decoding exists: the games draw their icons in code. The executable's own
icon (Explorer, shortcuts) is a resource compiled into it, and Linux's application menu reads a
`.desktop` file: both belong to release packaging.

## Options
**A. Sizes**
1. A list of images of one icon: the backend picks the closest per use (Win32) or passes them all
   (X11) — sharp at every size, the systems' own model.
2. One image, scaled by the system — simpler API; blurry in the title bar.

**B. When it is set**
1. A function callable any time, `rb_window_set_icon` — the icon can change later; the system's
   default shows for a moment at startup.
2. Fields of `rb_window_desc`, set at creation, before the window is shown — never the default
   icon, not even for a moment; with the application layer the game fills them in `configure`,
   so the pixels must outlive that call (static storage). A setter can follow when a game needs to
   change its icon: internally it is the same code.

**C. Pixel format**
1. RGBA, 8 bits per channel, straight alpha, rows from the top, as `rb_texture_desc` — one format
   for games to know; the backends convert.

**D. Who draws the icons**
1. Each game in its own code (Blink's view layer), the sandbox too; no engine default — the
   system's icon when none is given.
2. Image files in `games/<name>/assets/`, embedded at build time (a TGA decoder) or loaded at run
   time (file system, PNG decoder) — the first asset pipeline, for one icon.

## Decision
**A1, B2, C1, D1.**
- **Image type:** `rb_image` in `reborn/image.h`: width, height and a pointer to RGBA pixels it does
  not own. The type image decoding will produce later.
- **API:** `rb_window_desc` gains `const rb_image *icons` and `u32 icon_count`: images of one icon
  at different sizes, in any order; `NULL`/0 keeps the system's icon. They are read by
  `rb_window_create` only. A NULL `icons` with a count, or an image without pixels, is a bug in the
  caller (`RB_ASSERT`). Sizes of 0 or above `RB_WINDOW_ICON_MAX_SIDE` (256, the largest Windows
  icon), or more than `RB_WINDOW_ICON_MAX_COUNT` (8) images, are logged and the window gets the
  system's icon: it is still created.
- **Win32:** for the small and the big size of the window's DPI, the smallest image at least that
  large, else the largest (the system scales it); one `HICON` each from a 32-bit top-down DIB with
  straight alpha and an empty mask; set before `ShowWindow`, destroyed after `DestroyWindow`.
- **X11:** every image in `_NET_WM_ICON`, built in a temporary `unsigned long` array from the
  platform's allocator; set before `XMapWindow`. The conversion is a pure function, tested without
  an X server.
- **Games:** Blink draws its icon in the view layer at 16, 32 and 64 pixels; the sandbox has its
  own. Image files wait for the first game whose assets need them.

## Consequences
- No default icon flashes at startup; a game cannot change its icon yet (the setter goes to the
  backlog).
- The icon follows the DPI at creation: a window moved to a monitor with another scale keeps the
  sizes it got.
- The executable's icon and icons from files stay in the backlog.
