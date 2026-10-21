# Status

Append-only work log. Every entry is added at the end with an ISO 8601 timestamp. Existing entries are never edited or removed.

## 2026-10-21T02:09:20+09:00 — Baseline green, Phase 1 TDD seams confirmed

- Native baseline: 9/9 tests pass (`ctest --test-dir build/native`).
- Phase 1 seams confirmed: S1 Radar geometry (`NavPresenter`), S2 Radar rendering (headless LVGL framebuffer), S3 `DeviceSettings`.
- First red queue confirmed: rider anchor, building fill, full-canvas map, day/night theme, settings clamp/round-trip, low-speed Home/Settings gating.
- Next: write the first red test (`test_rider_is_anchored_at_the_centre_of_the_full_screen_radar`).

## 2026-10-21T02:09:35+09:00 — Red: rider anchor test fails as expected

- Added `test_rider_is_anchored_at_the_centre_of_the_full_screen_radar` to `tests/native/nav_presenter_tests.cpp`.
- `nav_presenter_tests` fails at `state.route_points[0].y == 233`; the old map-strip anchor is still in use.
- Next: move `kVehicleY` to the canvas centre and update the two existing anchor expectations to the new full-screen spec.

## 2026-10-21T02:10:01+09:00 — Green: full-screen Radar anchor

- `kVehicleY` is now `MOTO_UI_CANVAS_HEIGHT / 2` (233), so the rider sits at the physical centre of the Radar.
- Updated the two pre-existing anchor expectations in `nav_presenter_tests.cpp` to the new full-screen spec.
- `nav_presenter_tests` green; full native suite 9/9 green.
- Next red test: `test_a_building_footprint_interior_is_filled_with_the_theme_building_colour` (S2).

## 2026-10-21T02:10:49+09:00 — Red: building interior is not filled

- Added `test_a_building_footprint_interior_is_filled` to `nav_ui_render_tests.cpp` with a 4-point footprint at (100,100)-(200,200).
- The render test fails at `pixel_at(150, 150)` because the UI still draws only the footprint outline.
- Scope note: this slice fills with the current `kBuildingGray`; the theme palette switch remains a separate slice. The test is named for the behaviour (filled), not the future theme colour.
- Next: add a building-fill draw layer behind the outlines and repaint it when footprint geometry changes.

## 2026-10-21T02:12:47+09:00 — Paused by user instruction

- Development stopped at the user's request. No further code, test, branch, stash, commit, or remote changes were made after this entry.
- Current working tree:
  - Green (first slice): `shared/nav_presenter/src/moto_nav_presenter.cpp` and `tests/native/nav_presenter_tests.cpp` — rider anchored at (233,233); `nav_presenter_tests` and the other 8 native tests passed at this point.
  - Work in progress (second slice, RED): `shared/nav_ui/src/moto_nav_ui.cpp` adds a building-fill layer plus a two-pass triangle fan; `tests/native/nav_ui_render_tests.cpp` adds `test_a_building_footprint_interior_is_filled` plus a temporary pixel-grid debug dump.
  - The fill is working, but the fan's shared vertex still blends to `0x1924`/`0x18e3` instead of `0x2145` at (150,150). The test is intentionally red.
  - Docs created: `GLOSSARY.md`, `docs/adr/0001-attitude-telemetry-uplink.md`, `docs/adr/0002-rides-and-performance-runs-are-separate.md`, `status.md`, `todo.md`.
  - Also present: untracked `docs/ANALYSIS_AND_ROADMAP_GTA_ATTITUDE.md`; tracked `TODO.md` has a pre-existing modification not made by this session.
- Resume steps:
  1. Remove the temporary pixel-grid debug dump from `nav_ui_render_tests.cpp`.
  2. Kill the triangle-fan seam. The +1 px x-offset second fan still leaves a seam at (150,150); try offsetting the second fan on both axes, a hard-edged fill, or sampling a non-seam pixel and recording the limitation.
  3. Get `nav_ui_render_tests` green, then run the full native suite.
  4. Continue the confirmed red queue (full-canvas map, theme pixels, settings model, low-speed Home/Settings gating).
- All open tasks are paused.

## 2026-10-21T21:10:20+09:00 — Path correction: todo log is docs/todo.md

- macOS APFS is case-insensitive; creating root `todo.md` collided with the tracked upstream `TODO.md` and overwrote it on disk.
- Restored upstream `TODO.md` from HEAD; the append-only todo log now lives at `docs/todo.md`.
- No upstream TODO content was lost. The log content itself is unchanged.

## 2026-10-21T21:10:40+09:00 — Fork and feature branch synced

- `origin` now points to `https://github.com/JAHSEH618/moto-gps.git`; `upstream` retains the original `mx3353672833-debug/moto-gps-waveshare` repository.
- Pushed `main` (c8bdacc, already up to date) and `feat/gta-attitude` to the fork.
- Feature branch commits: `5065721` docs/glossary/ADRs/work logs, `da7fc81` full-screen radar anchor, `aad79fd` WIP building fill (intentionally red, temporary pixel debug dump).
- Local `main` now tracks `origin/main`; local `feat/gta-attitude` tracks `origin/feat/gta-attitude`.
- Working tree was clean before this log entry. Resume steps are unchanged: remove the debug dump, fix the triangle-fan seam, get the render test green, then run the full native suite.

## 2026-10-21T21:12:40+09:00 — Current progress analysis

- Phase 1 red queue (7 tests): 1 green (rider anchor), 1 red (building fill seam), 5 not started.
- Native suite: 8/9 pass; only `nav_ui_render_tests` is red.
- Phase 2 (attitude/telemetry) and Phase 3 (Ride/Performance Run) are design-only; no implementation yet.
- Biggest visual gap: the map layer is still `px(232)` = 300 px tall. The anchor moved to y=233, but the Radar is not yet full-screen and the HUD still uses the old split layout.
- Main risks: triangle-fan AA seam; red WIP commit on the feature branch; 500 m map radius will look sparse at full-screen density; e-scooter top speed is still unknown for 0-100 testing; font/licensing work is design-only.
- Recommended order: finish the building fill, expand the map layer and HUD, add the day/night theme, add Home/Settings plus the low-speed gate, then the `DeviceSettings` model; run the Web preview after the full-screen layout.

## 2026-10-21T21:14:31+09:00 — Green: building fill, debug dump removed

- Replaced the antialiased triangle fan with a hard-edged scanline span fill.
- The fill test now checks the centre and the two old fan-spoke points `(125,125)` and `(175,175)`; all are filled.
- Removed the temporary pixel-grid debug dump from `nav_ui_render_tests.cpp`.
- `nav_ui_render_tests` green; full native suite 9/9 green.
- Next red test: map covers the bottom of the round canvas (full-screen map layer).

## 2026-10-21T21:15:28+09:00 — Green: full-screen Radar canvas

- `nav_map`, the building fill, building outlines, roads, route shadow and route now use `MOTO_UI_CANVAS_HEIGHT` (466) instead of the old `px(232)` strip.
- The vehicle marker is repositioned so its bounding-box centre sits on the presenter's (233,233) rider anchor.
- Added `test_the_map_covers_the_bottom_of_the_round_canvas`; `nav_ui_render_tests` green and the full native suite is 9/9 green.
- HUD elements remain in the lower third and now overlay the map. The bottom vignette and the day/night palette are still pending.
- Next: build the Web preview, then add the day/night theme palette.

## 2026-10-21T21:21:18+09:00 — Green: GTA day/night map palettes

- Added `moto_ui_theme_t` and `moto_nav_ui_set_theme` to the shared UI API.
- Added `MapPalette` for Official, GTA Day and GTA Night; roads, buildings, route and the scanline fill read the active palette.
- Added a full-screen map land background object so the Radar theme does not recolor the speed/compass/music pages.
- Added `test_day_and_night_themes_render_different_map_colors`; full native suite 9/9 green.
- Web runtime builds successfully. Headless Chrome screenshots are blocked by `CVDisplayLinkCreateWithCGDisplay` in this environment; visual inspection remains manual in the browser.
- Next: `DeviceSettings` pure model (defaults, clamp, encode/decode).

## 2026-10-21T21:22:41+09:00 — Green: DeviceSettings pure model

- Added `platforms/esp32/main/device_settings.hpp/.cpp`: theme mode, brightness, units, Lean full scale, Performance Run target, page visibility and Lean zero offset.
- `clamp()` snaps to supported values (Lean 30/40/45/50, target 50/60/100, brightness 0–100, ±30° zero offset) and always keeps Navigation visible.
- Added a versioned 9-byte NVS blob with `encoded()` / `decode()`; unknown versions and short/tampered blobs fall back to defaults or clamp.
- Added `device_settings_tests`; full native suite is now 10/10 green.
- Phase 1 is 6/7: only the Home/Settings icon plus the low-speed gate remains; the settings page UI and NVS adapter are still pending.
- Next: Phase 2 `AttitudeEstimator` pure math seam (Lean, pitch, longitudinal/lateral G).

## 2026-10-21T21:23:38+09:00 — Green: AttitudeEstimator pure math

- Added `platforms/esp32/main/attitude_estimator.hpp/.cpp`: a stateless estimate from raw specific force plus the caller's filtered gravity vector in bike axes (x=right, y=forward, z=up).
- Lean right is positive; nose-up pitch is positive; longitudinal G is `(accel.y - gravity.y) / 9.80665`; lateral G is `tan(Lean)` clamped to ±4 g.
- Lean/pitch calibration offsets are applied; invalid gravity magnitude and non-finite inputs return `valid=false`.
- Added `attitude_estimator_tests`; full native suite is now 11/11 green.
- Next: map QMI8658 axes to the bike frame, wire the estimator into the sensor/bridge, and add the BLE `AttitudeSample` codec.
