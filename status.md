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

## 2026-10-21T21:25:09+09:00 — Green: BLE AttitudeSample codec

- Added `MessageType::AttitudeSample = 0x15` and `CapabilityAttitudeTelemetry = 1U << 8U`.
- Payload is the standard revision byte plus `u32 monotonic_ms` and four `i16` fields (Lean, pitch, longitudinal mg, lateral mg) = 13 bytes.
- Added validation (±180° Lean, ±90° pitch, ±4 g), round-trip tests, a golden payload (`0178563412b80b06ffcefe4102`) and an out-of-range rejection test.
- Updated the Chinese and English v1 protocol docs with the new message table row, capability bit, section 6.10 and golden payload.
- Full native suite is 11/11 green.
- Next: map QMI8658 axes to the bike frame and wire the estimator into `MotionHeadingSensor` / `PhoneNavBridge`; then decode the sample in the iOS BLE bridge.

## 2026-10-21T21:26:07+09:00 — Green: attitude data contract

- Added `has_attitude`, `lean_deg`, `pitch_deg`, `longitudinal_g` and `lateral_g` to `NavSnapshot` and `moto_ui_state_t`.
- `NavPresenter` only exposes attitude when every field is finite; Lean is clamped to ±180°, pitch to ±90°, G to ±4 g, and missing attitude zeroes the block.
- Added a presenter characterization test. Note: the shared fields landed before the assertion, so this slice is not a strict red-first cycle; the next UI slice will be.
- Full native suite 11/11 green.
- Next: PERF / LEAN page UI backed by the attitude block.

## 2026-10-21T21:28:54+09:00 — Green: PERF and LEAN pages

- UI page order is now Navigation → Performance → Lean → Speed → Compass → Music. The BLE `DisplayPage` appends `Performance=4` and `Lean=5` so existing wire values do not move.
- PERF page: `0-100 km/h` target, timer/best placeholders, live G-G dot plus LONG/LAT readouts.
- LEAN page: signed Lean, LEFT/RIGHT, 45° full-scale bar and session peak.
- Updated ESP32, Web and iOS page mappings; Web runtime builds successfully; full native suite 11/11 green.
- The PERF timer/best are placeholders until the phone sends Performance Run results, and the attitude block is not yet fed by the IMU bridge.
- Note: this page UI slice landed implementation-first because the enum/mapping change was cross-cutting; the render test is a characterization test. The next slice will be red-first.

## 2026-10-21T21:30:47+09:00 — Green: IMU attitude reaches the bridge and pages

- Added `sensor_to_bike` fixed-basis mapper with aligned, rotated and degenerate-basis tests.
- `MotionHeadingSensor` now computes an `AttitudeEstimate` at 125 Hz and passes it alongside the heading rate; `app_main` forwards it to the bridge.
- `PhoneNavBridge` stores the attitude block and presents motion even before the phone heading fusion is anchored; the bridge test confirms Lean/pitch/longitudinal/lateral values reach the UI state.
- Full native suite 11/11 green.
- The mount basis is a placeholder (screen-right = QMI X, screen-up = QMI Y). Board axis regression and Lean zero calibration are still pending.
- The ESP-IDF firmware build was not run in this environment; `motion_heading_sensor.cpp` and `app_main.cpp` need an `idf.py build` plus hardware validation.
- Next: BLE attitude telemetry sending + iOS decode, the 0-100 run pipeline, and the settings page/NVS/Home/Settings gate.

## 2026-10-21T21:33:59+09:00 — Green: PerformanceRunDetector

- Added the `MotoRideCore` Swift target and `MotoRideCoreTests`.
- The detector requires a one-second stationary hold, interpolates the start and target crossings, aborts on a >3 km/h speed drop, 30-second timeout or >2-second sensor gap, supports a configurable target (0-50 for the current scooter), and requires a fresh stationary hold between runs.
- 8 detector tests pass; the full Swift package suite is 24 tests (16 navigation + 8 ride) green.
- Next: wire the detector to raw CoreLocation speed, surface its live/result state on the PERF page, then add Ride recording/store/charts and the BLE telemetry/iOS decode path.

## 2026-10-21T22:40:12+09:00 — iOS App target builds for the first time

- Installed `xcodegen` 2.46.0, generated `platforms/ios/MotoGPS.xcodeproj`, and ran `xcodebuild -sdk iphonesimulator`: **BUILD SUCCEEDED**, 0 errors and 0 warnings (only an AppIntents metadata note).
- The whole app target — `App/*.swift`, the ObjC++ bridges, `ESP32BLECentral`, the `DisplayPage` mappings that grew two cases — compiles. Risk "iOS App target 没编译" is retired.
- Bundled fonts: `BarlowCondensed-SemiBold/ExtraBold` and `Anton-Regular`, registered through `UIAppFonts`; OFL licences copied to `LICENSES/`, notices updated in both languages.
- `.build/` and `MotoGPS.xcodeproj` were already gitignored, so the working tree stays clean.

## 2026-10-21T22:41:30+09:00 — Simulator screenshot loop established

- Downloaded the iOS 27.1 runtime; it turns out to be **Duo-only**: every portrait device type (`iPhone 17/17 Pro/Air/18 Pro/16 Pro/16/15/SE3`) fails with `Incompatible device`, while `iPhone Duo` is created automatically.
- The first boot rendered black (`Data Migration Failed`). `simctl erase` then reboot fixed the renderer; `simctl install` + `launch` + `io screenshot` now work, so SwiftUI can be verified visually from the agent.
- Added a `--moto-no-media-auth` launch argument to `AppleMusicRemoteController`: a system prompt cannot be answered from automation and the simulator has no music library. Product behaviour on a real device is unchanged.
- Downloaded the iOS 26.2 runtime (8.39 GB) in parallel for portrait devices and cross-version checks. The earlier 27.0 download was cancelled to save bandwidth.

## 2026-10-21T22:43:05+09:00 — Docker is on OrbStack; image pulls are not

- Both `/opt/homebrew/bin/docker` and OrbStack's `/usr/local/bin/docker` resolve to the `orbstack` context; the daemon reports `OrbStack overlayfs`, `linux/arm64`.
- Pulls hang regardless of image: `hello-world` timed out after 180 s, `espressif/idf:v5.5.5` sat at 0 bytes for 5 minutes, and four mirrors (`docker.1ms.run`, `docker.m.daocloud.io`, `dockerproxy.net`, `hub.rat.dev`) showed the same. `curl` to `auth.docker.io` and `registry-1.docker.io` returns 200/401 normally, so the stall is OrbStack VM egress, not the context or the registry.
- Pivoted the firmware gate to a native ESP-IDF 5.5.5 install (`~/esp-idf-5.5.5`); `github.com` and `dl.espressif.com` are reachable but the clone is very slow.

## 2026-10-21T22:45:20+09:00 — Design system v1: two themes, one structure

- Wrote `PRODUCT.md` (register: product; users, purpose, anti-references, five design principles) and `DESIGN.md` (theme switch, palettes, measured contrast, type scale, layout, circle motif, poster devices, HIG conformance, skill arbitration).
- Two themes, picked by ambient light rather than taste: **Vice** (dark instrument console, amber) and **Heat** (sunlit paper, near-black ink, hot orange). Heat is deliberately light: a dark UI behind glass in direct sun becomes a mirror.
- `MotoTheme.swift` holds the tokens: semantic text styles (`.body` 17, `.subheadline`, `.caption`/`.caption2` for the 11–12pt labels), `Font.custom(…, relativeTo:)` instead of fixed sizes, Increase Contrast variants for rules and secondary text, radius 0/2, no shadows.
- Retired the idea of an in-app appearance switch: HIG says an app-specific appearance setting makes people change two places. The theme follows the device, and falls back to the system appearance when the link is down.
- Wrote the arbitration for the four design skills in play (impeccable decides art direction, hallmark structure/anti-patterns, `design-swiftui-interfaces` SwiftUI correctness, `ios-design-agent-skill` iOS aesthetics), including three explicit rulings where they disagree.

## 2026-10-21T22:47:40+09:00 — GTA grammar extracted from reference repositories

- Studied `MickMelon/gta-sa-hud-react` (MIT code, 128 lines of components) and `Geckomacabre/GTA-VI-Inspired-HUD-for-FiveM` (FiveM resources, screenshots): outlined HUD type, 2px framed status bars, zero-padded counters, the two-tier turn card with a solid arrow tile, magenta route lines, the white speed-limit sign, and a framed minimap with semantic blips.
- Font verdict from the `name` tables in the shipped font files: Chalet London 1960 (`© 2001 House Industries`, paid, embedding not included), Aharoni (`use … as permitted by the license terms for the product in which this font is included`), Pricedown (`© Typodermic`, retail family). None of them grants a non-commercial branch, and bundling a font in an app is redistribution. The reference repo's MIT licence covers code only.
- Shipped **Anton** (OFL) as the wordmark face instead, with the outline reproduced as stacked zero-radius shadows. Recorded the whole mapping in `DESIGN.md` → "Reference grammar", including what was deliberately not copied.

## 2026-10-21T22:49:55+09:00 — Home and ride screens rewritten

- `HomeScreen.swift`: black slab status band with a 2px accent rule, 220pt radar carrying a street grid, seven building blocks, a park patch, water and a rider arrow, a 92pt Anton `GO` with a 2px ink outline and a hard drop, a solid accent command bar, and a numbered `01…06` pause-menu list.
- `ContentView.activeNavigationScreen`: GTA VI-style two-tier instruction card with a magenta arrow tile, a 196pt radar, `LEFT` / `ETA` numerals, ruled status rows, and a square abort-coloured end bar replacing the system capsule.
- Radar now reads as a minimap rather than a decorated disc; sweep changed from a hard pie slice to a gradient wedge rotating from twelve o'clock; screen grid gets a gradient mask so texture never sits under copy.
- The numbered list, the accent command block, the inverted badge and the 2px rules are recorded as "poster devices" in `DESIGN.md` so the route-preview screen cannot drift back to "quiet instrument".

## 2026-10-21T22:51:30+09:00 — Safehouse: home address, phone and protocol

- Added `HomeLocationStore` (UserDefaults, `PlaceSearchResult` JSON) and `AppModel.routeHome()`. The address never leaves the phone.
- BLE: `DeviceCommandKind::HomeShortcut = 20` in `shared/ble_protocol`, accepted by `valid_command_kind`, documented in the v1 protocol table, and covered by a new native test that pins the wire value and the round trip.
- `AppModel` handles kind 20 by planning a route home and answering `Ack Ok`; an unset home answers `.invalidState` — the failure mode is visible instead of silent.
- UI: a black slab house button beside `GO` (`SET HOME` / `HOME`), a safehouse glyph on the radar, a `06 家` menu row, and a `SET HOME ADDRESS` mode that turns the next picked search result into the saved home.

## 2026-10-21T22:53:17+09:00 — Fixes and current verification state

- Fixed a real palette bug: `ContentView` read `@Environment(\.motoPalette)` *above* the `.motoTheme(…)` modifier, so it resolved to the default dark palette and painted near-white text on the cream theme.
- `platforms/web/shell/styles.css` had no `:focus-visible` rule at all (8 buttons, 0 focus styles) — added a 2px amber outline; `prefers-reduced-motion` and `aria-label` coverage were already fine.
- Current state: native suite **11/11**; iOS App target builds with 0 warnings; static SwiftUI audit (`audit_swiftui_ui.py`) reports 30 MEDIUM and 0 HIGH/CRITICAL, and the six UI-level findings are in the code being replaced rather than patched.
- Still open: the round-screen home button (shared `nav_ui` callback + LVGL glyph + bridge send + render test), the SA status-bar palette, the route-preview screen, and the firmware build (blocked on ESP-IDF).

## 2026-10-21T22:56:22+09:00 — Append-only docs audited; two stale tasks closed

- Audited `status.md` and `docs/todo.md` mechanically: 0 deleted lines in both files and every timestamp parses. Ordering is by file position, not by timestamp — this batch was written at 22:56:22 and appended after the `22:58:40` entries, so the first version of this bullet was wrong about timestamps "increasing", and the entry counts it quoted went stale on the next append.
- One real defect: two `Resume:` tasks from `02:12:47` were still open even though the work landed at `21:14:31` (the debug dump was removed, the triangle fan was replaced by a scanline fill). Both are now closed by appending `DONE` entries that cite the original task and the status entry that recorded the work.
- One bundled task split: `21:25:09` Phase 2 covered both the QMI axis wiring (done at `21:30:47`) and the iOS `AttitudeSample` decode (not started). A `NOTE` records the split and a new `TODO` tracks only the decode.
- One convention deviation recorded: of the 28 `DONE` entries at that point, 7 were standalone work with no originating task (`Ref` not applicable), and 14 of the 21 closures restated the task instead of citing `Ref`. Back-filling would mean editing history, so the correction is a `NOTE` at the end of `docs/todo.md`, the rule that a closing `DONE` cites `Ref`, and `scripts/todo_status.mjs` to keep the open list derivable.
- Docs now agree with the code: native 11/11, iOS build 0 warnings, and the open queue is the round-screen radar layer, the SA bar palette, the route-preview rewrite, the iOS attitude decode, and the firmware build.

## 2026-10-21T23:02:20+09:00 — Audit checked against the files; the log now folds itself

- Re-read the previous audit instead of trusting it. Three of its claims were wrong: the log had 36 open tasks rather than a stable entry count, timestamps are not monotonic (this batch was appended after the `22:58:40` entries), and the `Ref` deviation is 7 standalone completions plus 14 uncited closures, not "21 of 26".
- Closed the third stale `Resume:` task (run the full native suite before the next seam), which the previous pass had missed; all three from `02:12:47` are now settled.
- `scripts/todo_status.mjs` folds `docs/todo.md` into the open list, and `--check` fails when a `DONE` looks like it closes a task without citing it. Timestamps are not unique (22 batches share one), so a marker resolves by timestamp plus text similarity; a `NOTE` closes an uncited or bundled task with `Closes`/`Supersedes`. `checks.yml` runs the check, so a stale task can no longer accumulate quietly.
- Resolved the log against the code, not against prose: 7 tasks closed (4 of them citing a named green test), 4 bundled entries superseded, and one genuinely untracked remainder split out — the PERF / LEAN pages still hard-code `kFullScaleDeg = 45` and the "0-100 km/h" label instead of reading `DeviceSettings`.
- Open queue: 15 tasks, down from 36 that mixed done, dropped and forgotten work. `TODO.md` and `docs/todo.md` now state their roles and link to each other; both checklist languages got the same line.
- Agent skills settled: `.agents/skills` and `skills-lock.json` are tracked, `.claude/skills` is a symlink mirror and is ignored.
- Verification before the commit: native **11/11**, Swift **24/24** (16 navigation + 8 ride), and `xcodebuild -sdk iphonesimulator` reports BUILD SUCCEEDED with 0 errors and no warnings beyond the AppIntents metadata note.

## 2026-10-21T23:05:30+09:00 — iOS attitude telemetry reaches Swift

- The bridge now maps `AttitudeSample` (0x15) into `MotoBLEAttitudeSample`. Before this the message was decoded and then dropped: `std::get_if<DeviceCommand>` returned null and the inbound object carried nothing, so the phone never saw an IMU sample.
- `MotoNavigationCore.AttitudeSample` owns the display units — `leanDegrees`, `pitchDegrees`, `longitudinalG`, `lateralG`, `leanSide`. No wire layout was copied into Swift; the Objective-C++ bridge keeps that job, as its header contract says.
- `AttitudeTelemetry` expires a readout after 500 ms and rejects one stamped in the future. A BLE link can go quiet without reporting a disconnect, and showing the last lean would tell the rider they are upright while the data is dead. `AppModel` also clears it on any snapshot that is not connected.
- `AttitudeSampleTests` adds 8 tests; the Swift package is now 32 green (24 navigation + 8 ride). `xcodebuild -sdk iphonesimulator` still reports BUILD SUCCEEDED with no new warnings.
- Split rather than closed: the original task also asked to feed the ride screen's meters, and no meters exist. The new TODO asks for that design decision before any layout work.
