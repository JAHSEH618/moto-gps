# Todo

Append-only task log. New tasks are added at the end with an ISO 8601 timestamp. Completion is recorded by appending a `DONE` entry that references the original task; original lines are never edited or removed.

- 2026-10-21T02:09:20+09:00 **TODO** — Phase 1 / S1: rider anchored at the full-screen radar centre (first red test).
- 2026-10-21T02:09:20+09:00 **TODO** — Phase 1 / S2: building footprint interior fill.
- 2026-10-21T02:09:20+09:00 **TODO** — Phase 1 / S2: map covers the bottom of the round canvas.
- 2026-10-21T02:09:20+09:00 **TODO** — Phase 1 / S2: day/night theme pixels.
- 2026-10-21T02:09:20+09:00 **TODO** — Phase 1 / S3: `DeviceSettings` defaults + clamp.
- 2026-10-21T02:09:20+09:00 **TODO** — Phase 1 / S3: `DeviceSettings` encode/decode round-trip.
- 2026-10-21T02:09:20+09:00 **TODO** — Phase 1 / S1: Home/Settings disabled above 5 km/h.
- 2026-10-21T02:09:20+09:00 **TODO** — Phase 1: Web preview + Radar layout mock.
- 2026-10-21T02:09:20+09:00 **TODO** — Phase 2: attitude estimator, BLE telemetry codec/gating.
- 2026-10-21T02:09:20+09:00 **TODO** — Phase 3: Performance Run detector, Ride recorder/store/charts.
- 2026-10-21T02:10:01+09:00 **DONE** — Phase 1 / S1: rider anchored at the full-screen radar centre (first red test). Ref `2026-10-21T02:09:20+09:00`.
- 2026-10-21T02:12:47+09:00 **PAUSED** — All Phase 1–3 tasks paused by user instruction. No code changes until resumed.
- 2026-10-21T02:12:47+09:00 **PAUSED** — Phase 1 / S2: building footprint interior fill. Ref `2026-10-21T02:09:20+09:00`. Current state is RED: fill renders, but the triangle-fan centre seam fails `pixel_at(150,150) == 0x2145`.
- 2026-10-21T02:12:47+09:00 **TODO** — Resume: remove the temporary pixel-grid debug dump from `tests/native/nav_ui_render_tests.cpp`.
- 2026-10-21T02:12:47+09:00 **TODO** — Resume: fix the building triangle-fan seam and get `test_a_building_footprint_interior_is_filled` green.
- 2026-10-21T02:12:47+09:00 **TODO** — Resume: run the full native suite before starting the next seam.
- 2026-10-21T21:10:20+09:00 **NOTE** — File lives at `docs/todo.md`; root `todo.md` collides with upstream `TODO.md` on case-insensitive macOS APFS. Upstream `TODO.md` was restored from HEAD.
- 2026-10-21T21:10:40+09:00 **DONE** — Synced fork `JAHSEH618/moto-gps` and branch `feat/gta-attitude`. `main` is at `c8bdacc`; the feature branch is at `aad79fd`.
