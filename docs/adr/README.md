# Architecture Decision Records

One decision per file (a topic, with all its sub-decisions). The status says whether it may still change:

| Status | Meaning | Editable |
|---|---|---|
| Proposed | Being designed or implemented; options and decision are refined as we learn. | Yes |
| Accepted | Implemented and verified. | No: a change needs a new ADR |
| Rejected | Considered and dropped; kept as a record of why. | No |
| Superseded by NNNN | Replaced by a later ADR. Only the status line changes. | No |

An ADR may be committed while still *Proposed*. It becomes *Accepted* in the commit that completes
its implementation.

| # | Title | Status |
|---|---|---|
| 0001 | [Build system, compiler and C standard](0001-build-system-compiler-standard.md) | Accepted |
| 0002 | [Static engine library](0002-static-engine-library.md) | Accepted |
| 0003 | [Versioning and repository layout](0003-versioning-and-repo-layout.md) | Accepted |
| 0004 | [Unit testing](0004-unit-testing.md) | Accepted |
| 0005 | [Linux build container](0005-linux-build-container.md) | Accepted |
| 0006 | [Platform layer](0006-platform-layer.md) | Proposed |
| 0007 | [Generational handles and index-only containers](0007-index-only-containers.md) | Accepted |
| 0008 | [Logging and asserts](0008-logging-and-asserts.md) | Accepted |
| 0009 | [Monotonic clock and sleep](0009-monotonic-clock-and-sleep.md) | Accepted |
| 0010 | [Memory allocation](0010-memory-allocation.md) | Proposed |

Template: [`template.md`](template.md).
