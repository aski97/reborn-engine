# Architecture Decision Records

One decision per file (a topic, with all its sub-decisions). The status says whether it may still change:

| Status | Meaning | Editable |
|---|---|---|
| Proposed | Being designed or implemented; options and decision are refined as we learn. | Yes |
| Accepted | Implemented and verified. | No: a change needs a new ADR |
| Rejected | Considered and dropped; kept as a record of why. | No |
| Superseded by NNNN | Replaced by a later ADR. Only the status line changes. | No |

Editorial fixes that change no decision (wording, references) are allowed in any status.

An ADR may be committed while still *Proposed*. It becomes *Accepted* in the commit that completes
its implementation.

| # | Title | Status |
|---|---|---|
| 0001 | [Build system, compiler and C standard](0001-build-system-compiler-standard.md) | Accepted |
| 0002 | [Static engine library](0002-static-engine-library.md) | Accepted |
| 0003 | [Versioning and repository layout](0003-versioning-and-repo-layout.md) | Accepted |
| 0004 | [Unit testing](0004-unit-testing.md) | Accepted |
| 0005 | [Linux build container](0005-linux-build-container.md) | Accepted |
| 0006 | [Platform layer](0006-platform-layer.md) | Accepted |
| 0007 | [Generational handles and index-only containers](0007-index-only-containers.md) | Accepted |
| 0008 | [Logging and asserts](0008-logging-and-asserts.md) | Accepted |
| 0009 | [Monotonic clock and sleep](0009-monotonic-clock-and-sleep.md) | Accepted |
| 0010 | [Memory allocation](0010-memory-allocation.md) | Accepted |
| 0011 | [Application layer and game loop](0011-application-layer-and-game-loop.md) | Accepted |
| 0012 | [Allocation tracking](0012-allocation-tracking.md) | Accepted |
| 0013 | [Vulkan renderer](0013-vulkan-renderer.md) | Accepted |
| 0014 | [Games and sandbox](0014-games-and-sandbox.md) | Accepted |
| 0015 | [Shaders and graphics pipeline](0015-shaders-and-graphics-pipeline.md) | Accepted |
| 0016 | [Math library and 2D camera](0016-math-and-2d-camera.md) | Accepted |
| 0017 | [GPU memory and textures](0017-gpu-memory-and-textures.md) | Accepted |
| 0018 | [Bitmap font and glyph atlas](0018-bitmap-font-and-glyph-atlas.md) | Accepted |
| 0019 | [Random number generator](0019-random-number-generator.md) | Accepted |
| 0020 | [Window icon](0020-window-icon.md) | Accepted |
| 0021 | [Text rendering](0021-text-rendering.md) | Accepted |
| 0022 | [Command system](0022-command-system.md) | Accepted |

Template: [`template.md`](template.md).
