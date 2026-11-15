# 0003 — Versioning and repository layout

- **Status:** Accepted
- **Date:** 2026-11-15

## Context
We want traceable changes, an always-known engine version, and a repository that is
easy to navigate for both people and LLMs.

## Decision
- Git, `main` branch, a `vX.Y.Z` tag per release.
- SemVer. While in `0.x` the API is unstable and any MINOR may break it.
- Version lives only in the root `project(... VERSION)`; CMake generates `reborn/version.h`.
- Keep a Changelog (`[Unreleased]` becomes the release section when tagging).
- Conventional Commits (`feat:`, `fix:`, `docs:`, `build:`, `refactor:`, `test:`, `chore:`).
- Public API in `engine/include/reborn/`, private code in `engine/src/`; CMake enforces the boundary.

## Consequences
- Code and build can never disagree on the version.
- Structured commit messages allow generating the changelog automatically later.
