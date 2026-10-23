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

## 2026-10-21T23:08:40+09:00 — Safehouse reaches the round screen

- `moto_nav_ui` owns a `moto-home` control on the navigation page: a 46 pt circle with the Montserrat home glyph, drawn above the radar and below the connection overlay. `moto_nav_ui_set_home_callback()` is the only new surface.
- The gate is speed, not intent: the glyph is hidden above 5 km/h and `home_shortcut_event()` refuses a tap anyway. A queued or synthetic event must not replace active guidance with a plan home, so hiding it is not the whole rule.
- `PhoneNavBridge::home_command()` sends `HomeShortcut` with `AckRequested | Urgent` through the existing command path, so it inherits command-id allocation and the link-active check. A dead link sends nothing rather than queueing an intent that fires later.
- Two tests: `nav_ui_render_tests` taps the real LVGL object by name (via `lv_obj_find_by_name`, which needed `LV_USE_OBJ_NAME` in the shared `config/lv_conf.h`), and `phone_nav_bridge_tests` asserts the wire command, its flags, and the dead-link case. Native suite 11/11.
- Still open on purpose: the glyph never reflects whether a home is set, because the device cannot know. The phone's `.invalidState` Ack is dropped after logging, so a tap can look like a no-op; a TODO tracks that feedback instead of faking a disabled control.

## 2026-10-21T23:32:20+09:00 — Design pass: the radar stops being decoration

- Ran the impeccable product-register pass against real screenshots (iOS 26.2 iPhone 17 Pro, iOS 27.1 iPhone Duo, Vice and Heat), not against the code. The first look found a functional bug before any styling: the status band truncated the clock to `23…` on a 393pt phone.
- Root cause of "not designed": the light theme inherited alphas tuned for dark. Blocks at 10% ink on cream disappear, so the circle read as a watermark. Map strength is now a per-theme table in DESIGN.md, and Heat dropped its band texture entirely — thin bands in glare were the haze over the disc.
- The ride radar was `MotoRadar(size: 196, route: [])`: a screenshot of a poster, next to a live route. It now projects the real route window plus the real map-scene streets and footprints through one rider-centred transform. `SurroundingMapStore` retains its latest window so the phone can draw the same geometry the terminal gets.
- Progress: a separate arc outside the ring read as a halo, so the ring itself became the gauge — the completed fraction of `route_progress_m / total_distance_m` replaces that segment of the 2px rim in accent. N badge on the rim, safehouse blip inside it.
- Hierarchy fixes: `GO` outline is `slab` (near-white on near-black read as a glow), wordmark 88 with the home control at 48 on the same baseline, one-label command bar at 56pt, `onAccent` token so text on accent is 5.0:1 in Heat instead of 3.9:1, units lowercase at the numeral baseline, `GATEWAY` instead of the error-sounding `NOT READY`, ride screen gains a speed cell and drops the duplicate demo caption.
- Removed dead weight rather than leaving it: the unused `MotoCommandBar`, the `BandTexture` and the `RadarTexture.bands` case.
- Verification: iOS sim build 0 errors / 0 warnings (AppIntents metadata note only), Swift 32 tests green, screenshots checked at both device sizes and both themes. Not yet re-checked: VoiceOver ordering on the new radar contents, and the Increase Contrast variants of the new map alphas.

## 2026-10-22T00:16:50+09:00 — Simulator cleaned; Chinese tofu traced to the 26.2 runtime

- Cleanup, as instructed: deleted the throwaway devices (fresh 17 Pro, 16e, all iPads, the unavailable 16-series leftovers), cleared the CoreSimulator cache directory and the 7.8 GB exported runtime bundle, and removed the font experiment. Four devices remain — iPhone 17 Pro, iPhone 17, iPhone Air on 26.2, iPhone Duo on 27.1.
- The tofu is not the app. The springboard draws boxes for Chinese app names on the same device, and CoreText logs the exact failure: it resolves `.PingFangUITextSC-*` from `FontServices.framework/CorePrivate/PingFangUI.ttc`, then reports "Requested font not available … fallback to LastResort". The identical build renders Chinese on 27.1.
- Tried, failed, reverted — nothing ships: `CFBundleLocalizations: [en, zh-Hans]`; registering the runtime's own `PingFangUI.ttc` for the process; switching the simulator to `zh-Hans-CN` and rebooting; re-exporting the 26.2 runtime. None made the private face loadable, which matches the working assumption that the runtime lacks a usable GB/Chinese face.
- Consequence: copy and typography are judged on iPhone Duo (27.1); 26.2 stays for phone-shaped layout and anything that does not need Chinese. A TODO records the open decision — repair the runtime or keep the split review.
- Paused here, as instructed. The two next product asks are recorded as TODOs: the theme switch with device sync, and replacing the phone's circular radar with a rounded-rectangle live map.

## 2026-10-22T00:55:00+09:00 — Batch 0: the review matrix, the font gate, the verify gate, the settings contract

- **The Chinese problem was an unregistered runtime, not a broken one.** iOS 27.0 (24A434) had been downloaded on 2026-10-21 22:18 into `/System/Library/AssetsV2/com_apple_MobileAsset_iOSSimulatorRuntime/3e800c38….asset` and never registered. `xcrun simctl runtime add …/AssetData/Restore/043-70362-657.dmg` registered it; unlike 27.1 it accepts portrait device types (17.1 rejects all five I tried), and its `PingFangSC-Regular` reports **47,102 glyphs** with 中 → glyph 183. The app renders Chinese on a 402×874 phone; SpringBoard does too. No app-side font hack ships.
- **Why the earlier attempts could not work**, now measured rather than assumed: on 26.2 exactly **24 of 303** registered faces fail — every PingFang, nothing else — and the file itself is complete (32 faces, 20,992 Han codepoints, `maxp` 47,098 == glyph order length, all tables inside EOF, same table set and `cidg`/`hvgl` v3 versions as the working 27.1 file). Registering Noto Sans SC in-process succeeds and gives 30,898 glyphs, but `CTFontCreateForString(zh-Hans)` still returns `.PingFangUITextSC-Regular`; impersonating that name is refused by `CTFontManager` (error 305). The failure is inside the runtime's font stack, below every API an app can reach.
- **`scripts/sim_font_gate.sh`** turns that into a 2-second check: it builds a CoreText probe against the simulator SDK, runs it inside a booted device, and exits 2 with a "layout-only" explanation when PingFang is a 7-glyph LastResort stub. A screenshot review of Chinese starts with it instead of with a memory of which runtime is broken.
- **`scripts/verify.sh`** runs the whole gate locally: todo-log check, native configure/build/ctest, Swift package tests, and the iOS app build. Warm run **36 s**, one log per step, the failing log tailed, `--install-hook` writes `.git/hooks/pre-push`. `--no-ios` exists for a fast loop.
- **Review matrix** (all runtimes whose probe passes, except the last): iPhone SE 375×667, iPhone 17 Pro 402×874 and iPhone Air 420×912 on 27.0; iPhone Duo on 27.1; iPhone 17 Pro on 26.2, kept only for iOS 26 compatibility and marked layout-only. The devices the 27.0 runtime auto-created, and the two redundant 26.2 phones, are deleted.
- **BLE `SettingsState` (`0x17`) contract**, the piece #1/#13/#18 all need: capability bit 10, 14-byte fixed payload (`theme`, `brightness_percent`, `units`, `lean_full_scale_deg`, `performance_target_kph`, `visible_pages`, `lean_zero_offset_cdeg`, `settings_revision`). The device is the authority: it applies a higher revision and echoes the applied state, ignores anything it has already applied, and both ends may originate. Values are a fixed table, not a range — 35° or 75 km/h is refused rather than rounded into a gauge nobody drew. Golden vector `01034101283c001f83ff05000000`; `device_settings_tests` pins `moto::device::DeviceSettings` to the wire struct field by field, including that their defaults agree.
- **Two corrections to the log.** `HomeLocation` (0x16) shipped without a §5 row or a §6.11 section although the `23:16:40` entry claims it was documented; both languages now cover 0x16 and 0x17 and list capability bits 9 and 10. And the CI TODO is superseded rather than done: `.github/workflows/ios-sideload.yml` has built the app and run UI tests since 2026-09-28, so the app build was never the gap — this fork simply has no Actions (`gh workflow list -R JAHSEH618/moto-gps` is empty, the upstream's last six runs are `action_required` with 0 s). The open question is now explicit: enable Actions on origin, or document that `verify.sh` is the gate.
- Verification: native **11/11**, Swift **32**, iOS simulator build **BUILD SUCCEEDED** with the pre-existing AppIntents metadata note only, `todo_status --check` clean. Open queue **18**.
- Not in this batch on purpose: NVS persistence, the settings page, the theme UI, the iOS decode of `0x17`, and the device advertising `CapabilityDeviceSettings` — a firmware that advertises a setting it cannot apply would be worse than one that never sees the message. Those are Batch 2. Next is Batch 1: the `HomeLocation` sender, the safehouse blip, the round-screen ring devices, and the refused-home Ack.

## 2026-10-22T01:29:43+09:00 — Batch 1 half-landed; the render suite exposed a teardown defect

**Landed and verified — the phone half of the safehouse.**
- `HomeLocationBroadcast` (MotoNavigationCore) owns capability bit 9, the revision that skips zero, and the single WGS84→GCJ-02 conversion. `ChinaCoordinateTransform` moved from the app target into the package, so the conversion the device depends on is covered by `swift test` (Beijing offset, round trip, identity outside mainland China, and the size of a double conversion).
- The Objective-C++ bridge gained `MotoBLEHomeLocationInput`, `encodeHomeLocation:` (coordinate system pinned to GCJ-02, no Ack: the message is idempotent and ordered by revision) and an `encodeHomeLocationPayloadForTesting:` hook; `BLEProtocolGoldenCheck` now also compares `home.payload`.
- `ESP32BLECentral` keeps the newest broadcast, gates on `CapabilityHomeLocation`, sends it once per revision and re-sends after every handshake (`lastSentHomeRevision` is cleared on protocol-ready and teardown). `HomeLocationStore` persists the revision and starts an older install at 1 when a home exists, so a restart cannot rewind the sequence below what the device already applied. `AppModel` broadcasts on init and on every store write, because views write the store directly.
- Verified: 41 Swift tests green (33 navigation + 8 ride) and `xcodebuild -sdk iphonesimulator` BUILD SUCCEEDED. One real link failure on the way: the new ObjC input class declared an interface without an `@implementation`, so the symbol never existed. The `CoreAudioTypes` / `UIUtilities` / `SwiftUICore` link warnings in that log are pre-existing noise from the 27.1 SDK, not this work.

**Landed and verified — the refusal path.**
- `send_device_command` now returns the command id it wrote, `home_command()` remembers it, and an Ack for that id with a non-Ok status raises `RenderHomeError`; the render task calls `moto_nav_ui_show_home_error()`. `phone_nav_bridge_tests` covers refused, unrelated, repeated and accepted Acks. A tap that the phone refuses no longer looks like a dead control.

**Written, unverified — the screen half.** Full-ring progress arc starting at twelve o'clock in the traffic colour (its only traffic surface), a north badge that rides the ring at the true-north bearing (fixed north, as chosen), the safehouse blip clamped to the ring instead of hidden, a three-second `SET HOME ON PHONE` hint, and a dev-only `MOTO_UI_DUMP` PPM frame dump.

**The render suite cannot run, and the reason is a real firmware defect, not a test.**
- Attaching `lldb` to the spinning process gave the first stack: `moto_nav_ui_set_state → show_page → lv_obj_remove_flag(nullptr)` after a terminal screen had cleaned the tree. Guarded: `show_page` now returns when the page object is missing.
- The second stack, after that guard, was `moto_nav_ui_set_state → update_navigation → update_building_geometry → lv_obj_add_flag(nullptr)`. The root cause is that `moto_nav_ui_show_power_off_screen()` cleans the widget tree **without** `reset_ui_state()`, so `ui.pages[]` and every `ui.nav_*` pointer stay non-null while pointing at deleted objects; a pointer-null check cannot see that. `moto_nav_ui_set_state` also never got the guard the other mutators have (that edit silently did not apply — worth remembering that a patch script with no assertion can report success and change nothing).
- This matters beyond the tests: on the board, `show_power_off_screen()` runs while the BLE task can still deliver a snapshot, and the firmware would apply it to freed LVGL objects. The fix is one `ui.live` gate plus a reset of the pointers in both terminal screens, not more pointer probes at each call site.
- Why it presented as a timeout: `LV_ASSERT_HANDLER` is not overridden in this build, so LVGL's null-object assert is a `while(1)` spin at 100 % CPU. Every failure of this class looks like a hang; only `lldb --batch -o "process attach --pid N" -o "thread backtrace"` shows it.
- Conditions the render suite needs, recorded in `docs/todo.md` so they are not rediscovered: `TARGET lvgl`, `LV_COLOR_DEPTH 16`, `LV_DEF_REFR_PERIOD 25`, `LV_USE_OBJ_NAME 1`, 8 MiB LVGL heap, boot screen settled before `moto_nav_ui_create()`, one shared `Ui` that each test must leave settled, terminal screens last.

**State.** Working tree dirty, nothing committed this turn: 17 modified files and 3 new Swift files. No test run is claimed for the screen half; the native suite was last green before the ring work (`11/11`), and the Swift package is green at 41. Next is the teardown gate, then the three render checks, then one look at a dumped frame before any screenshot review.

## 2026-10-22T01:40:31+09:00 — The teardown gate lands; the render suite runs and finds a heap budget

**The gate.** `moto_nav_ui.cpp` now has exactly one way out of the widget tree. `teardown_ui()` deletes the five timers that retain widget pointers (`page_dots`, `route_motion`, `lifecycle`, `success`, `home_hint`), clears every pointer through `reset_ui_state()`, re-acquires the active screen and cleans it. The boot screen, the power-off screen and `moto_nav_ui_create()` all call it; the two terminal screens no longer carry a copy of the timer-deletion block each, and `create()` now retires a previous tree instead of assuming it starts from nothing.

Pointer clearing alone would have fixed the reported crash, because every existing check (`pages[page] == nullptr`, `nav_lifecycle == nullptr`, the `show_page` early return) starts working again once the pointers are null. The new `bool live` is what covers the paths a pointer test cannot see:

- `gesture_event` and `interaction_event` are registered on `ui.screen`, which `lv_obj_clean()` does not delete. `reset_ui_state()` does not unhook them either, so a touch during the shutdown window reached `show_page()` with dangling page objects. Both now check `ui.live`, and so does every public mutator that touches widgets.
- `create()` re-adds the screen callbacks on every call, so a second create would have stacked a duplicate gesture handler (one swipe, two pages). They are removed before they are added.
- The route early-out is the only branch that may legitimately see an absent object while live; its three `lv_obj_add_flag` calls are now null-checked. The other two geometry loops are not, on purpose: `live` is the single owner of that invariant, and a second copy of it would let a future deletion of the gate stay green.

**Four traps the task text did not have.**

1. `ui.live = true` at the end of `create()` would have disabled the two public calls near the end of that same function (`set_music_state`, `set_state`) and booted into an unstyled page — with the suite still green, because every test applies its own state. It is set after `install_interaction_wake()`.
2. `show_power_off_screen()` cannot gain a `reset_ui_state()` at the top: reset nulls `ui.screen` and the next `lv_obj_clean()` would dereference null. The reset lives inside `teardown_ui()`, which re-acquires the screen afterwards.
3. `reset_ui_state()` clears the four callbacks along with everything else. That makes "install callbacks after `create()`" an implicit contract — true on ESP32 (`install_ui_callbacks()` follows `create()`) and on the web runtime, now written down where the memset happens.
4. The shutdown window on the board is not where the log said. `app_main.cpp` calls the power-off screen while holding the display lock, so a queued render cannot interleave there; the exposure is the `board_port_unlock()` → `esp_deep_sleep_start()` gap (~800 ms plus the key-release wait), which is plenty for a pending snapshot. The defect was real, the mechanism was off by one lock.

**Tests.** `nav_ui_render_tests` went from a suite that could not run to 17 cases green:

- New `test_rebuilding_the_ui_is_idempotent`: create, apply a state, capture; create again, apply the same state, capture; the frames must match. This is the second-create path, which nothing covered.
- The ring test now samples a 25% arc on both diagonals, because at twelve o'clock a clockwise and a counter-clockwise arc paint the same pixel; and it checks the badge at 180° and 270°, where a sign error would otherwise survive.
- The blip test is renamed to `..._is_clamped_to_the_ring` (the old name said "visible only inside", which is the policy it replaced) and asserts the glyph stays light after switching to the GTA day theme, the one constraint the theme switch could silently break.
- A visible `SET HOME ON PHONE` hint is raised immediately before the terminal screens, so the hint timer is exercised in the worst case rather than only when hidden.
- `render_demo_frame()` now sets theme and page explicitly instead of inheriting whatever the last test left.

**What the first run taught us.** Six failures, five of them one cause: a link that comes up after `create()` (which resets the connection to offline) raises the 920 ms `CONNECTED` overlay, and that overlay outranks active guidance, so it covered the next test's frame. It is correct behaviour; frame-comparing tests have to let it expire. The sixth was my own assertion hard-coding pure white — the blip draws `kWhite`, which is a warm `0xF7`, so the check now asks whether the glyph is still light instead.

The assert-handler change earned its keep immediately: `-DMOTO_NATIVE_LVGL_HEAP_BYTES=65536` did not hang, it trapped in `lv_event_add` and `lldb --batch -o run -o "bt 12"` showed the frame. `LV_ASSERT_HANDLER` is `__builtin_trap()` for the native build only, wired in `tests/native/CMakeLists.txt`, and the ctest entry now has a 180 s timeout so a deadlock fails instead of occupying the default 1500 s.

**Verification.** native **11/11** (the render suite included), `nav_ui_render_tests` **17/17**, `node scripts/todo_status.mjs --check` clean, and the `MOTO_UI_DUMP` frame rendered and reviewed: at a 35° heading the `N` badge sits up-left on the rim, the amber arc runs from twelve o'clock to seven o'clock for 62% progress, and the safehouse blip sits inside the ring.

**New finding — the tree does not fit the board's LVGL pool.** `MOTO_UI_HEAP=1` reports `total=8383200 free=8372880 max_used=75328`. The ESP32 build gets `CONFIG_LV_MEM_SIZE_KILOBYTES=64` (LVGL Kconfig default, not overridden in `sdkconfig.defaults`) with `LV_MEM_POOL_EXPAND_SIZE=0`, so `moto_nav_ui_create()` would assert on the device before any snapshot arrives. The 64 KiB native build reproduces it. This is a budget problem rather than a regression from this batch — the four ring devices are a small fraction of 75 KiB — and it is filed as a TODO rather than papered over by raising the number without measuring.

**State.** Working tree carries 19 modified files and 3 new Swift files, nothing committed. The next step is no longer the render suite; it is the firmware build (still blocked on ESP-IDF) so the heap decision is made against a real `idf.py build`.

## 2026-10-22T01:44:29+09:00 — Batch 1 committed and pushed

**Two commits on `feat/gta-attitude`, `origin` now at `33a9a63`.**

- `2473e82` *feat(nav_ui): make the rim a gauge and give teardown one exit* — 4 files, +571 −97. The ring, the north badge, the clamped safehouse blip, the three-second home hint, and the teardown gate that makes the suite that verifies them runnable.
- `33a9a63` *feat(ble): send the saved home to the device and surface a refusal* — 19 files, +594 −93. `HomeLocationBroadcast`, the single WGS84 → GCJ-02 conversion in `MotoNavigationCore`, the bridge encoder with its golden vector, the capability gate and revision persistence, the Ack → `RenderHomeError` path, and the log entries for the batch.

**The split is by surface, not by the gate / screen grouping I proposed first.** The two topics are interleaved line by line in `moto_nav_ui.cpp` — `teardown_ui()` deletes `nav_home_hint_timer`, which the ring work introduced — and the dependency runs one way: `phone_nav_bridge.cpp` calls `moto_nav_ui_show_home_error()`, declared by the UI commit. Gate-first would need a third commit for the bridge and would ship a commit that fixes a widget tree the next commit replaces. Device-then-wire gives two commits that each stand alone.

**Verification at push.** The pre-push hook ran `scripts/verify.sh`: todo log clean, native **11/11**, Swift **41**, iOS app build succeeded. The 17 render cases ran as part of the native step; the hook is the gate that actually executes in this fork, since `origin` has no Actions.

**Recorded, not fixed.** Two gaps are now explicit in the log. The LVGL pool high-water mark is 75,328 bytes against the board's 64 KiB, so `moto_nav_ui_create()` still needs a budget decision before the next firmware build. And the gesture dedupe added by this batch is guaranteed by construction rather than by a test, because the render harness has no indev to send a gesture through.

## 2026-10-22T02:22:22+09:00 — AUTO gets an input, the heap question closes, the swipe is tested

**The LVGL heap finding was wrong, and the suite now proves it.** The log said the ESP32 build gets `LV_MEM_POOL_EXPAND_SIZE=0` and would assert inside `moto_nav_ui_create()`. `config/lv_conf.h` sets it to 2 MiB, and `board_port_init()` adds a 2 MiB PSRAM pool with `lv_mem_add_pool()` before the UI is created, so the device runs 64 KiB internal plus 2 MiB PSRAM — which is exactly what `lv_tlsf.c`'s `TLSF_MAX_POOL_SIZE` is sized for. The render suite now models the board instead of an 8 MiB fiction: `MOTO_NATIVE_LVGL_HEAP_BYTES=65536` plus a 2 MiB pool on the ctest command line, reporting `total=2157776 free=2147432 max_used=76448`. The 64 KiB-only build still traps in `lv_event_add`, and that is the whole story: the missing second pool was the harness, not the device. No sdkconfig change, no object trimming, no RAM accounting.

**AUTO without a clock would have been a lie.** `MOTO_UI_THEME_AUTO` is in `moto_ui_theme_t` and `moto_nav_ui_set_ambient_night()` supplies the phone's day/night flag; `moto_nav_ui_set_theme()` resolves AUTO in one place, an explicit theme ignores the flag, and a repeated flag does not repaint. The flag is cleared by `reset_ui_state()`, so it follows the same contract as a callback: re-apply after `create()`. That is the socket Batch 2 plugs the settings replay into; `SettingsState` still has no day/night byte, so AUTO is not yet reachable over BLE.

**The swipe is tested with a real gesture, not a counter.** `LV_USE_TEST=1` on the native lvgl target compiles LVGL's own `lv_test_indev.c`, the only way to send `LV_EVENT_GESTURE`: the event cannot be synthesised with `lv_obj_send_event`, because `gesture_event()` reads the direction from the active indev. `test_one_swipe_moves_one_page_even_after_a_rebuild` drags through the mouse indev and asserts exactly one page before and after a second `create()`, plus one page back in the reverse direction. It was mutation-checked: deleting `lv_obj_remove_event_cb(ui.screen, gesture_event)` from `create()` turns one swipe into two pages and two checks fail, so the dedupe is now guarded by a test rather than by construction.

**Two decisions moved the plan.** The handlebar gets no settings page — the phone is the settings surface, so the "settings page UI" half of the 21:22:41 task is dropped and the log carries a narrowed Batch 2 task (NVS store, apply after `create()`, capability bit 10, a one-byte day/night field, a phone-side Auto / Day / Night control). Units, brightness and the page mask stay out until each has a sender. And 26.2 being layout-only has no bearing on installation: the broken face lives in the simulator runtime asset, real devices ship an intact system font, and the deployment target is 17.0; what 26.2 cannot judge is Chinese typography, which `docs/TESTING.md` now states as a rule.

**Verification.** `ctest` 11/11 with the render suite at 19 cases, the 64 KiB-only build reproduced intentionally (exit 133, SIGTRAP in `lv_event_add`), `node scripts/todo_status.mjs --check` clean at 10 open tasks, and `scripts/verify.sh` green through todo log / native / Swift / iOS.

**State.** Committed on `feat/gta-attitude` and pushed to `origin`.

## 2026-10-22T09:03:06+09:00 — Local review session torn down

**Stopped, on request.** The demo at `http://127.0.0.1:4173/platforms/web/shell/index.html?demo=1` was served by a bare `python -m http.server 4173 --directory .` — not by a script in this repo, so there is nothing to uninstall. The process (PID 96045) is stopped and port 4173 is free.

**The simulator set is now empty.** The Simulator app was quit and all nine devices were deleted: 26.2 iPhone 17 Pro; 27.0 iPhone 17 Pro, iPhone Air, iPad Pro 11-inch (M5), iPad mini (A17 Pro), iPad Air 13-inch (M4), iPad Air 11-inch (M4), iPad (A16); 27.1 iPhone Duo. The three runtime images stay — 26.2, 27.0 and 27.1 — because deleting them would discard the hand-registered 27.0 runtime that fixed Chinese review, and they are multi-GB downloads.

**Two consequences, both recorded in the log.** The device matrix claimed at `00:55:00` no longer exists on disk, and it was already inaccurate: the iPhone SE it names was gone, and five iPads had come back after that entry said they were deleted. So `2026-10-21T22:41:30+09:00` (re-shoot every screen on 26.2 and 27.1) is blocked on `xcrun simctl create` rather than on the app. And `scripts/sim_font_gate.sh` reports a setup failure (exit 1) with no booted device — that is not the "layout only" verdict (exit 2), and it should not be read as one.

**State.** No code changed. Only this log, `docs/todo.md` and this section.

## 2026-10-22T23:05:00+09:00 — Batch 2 device half: settings survive the reboot, and buildings become opt-in

**The map program got a written plan first.** Six entries in `docs/todo.md` record what the `zyn950929/MotoNav` comparison actually implies here, because most of it does not transfer: that repo ships a **raster** pipeline (1200×1200 classified texels per 2.4 km) while this one ships **vector** geometry. Two findings decided the shape of the work. First, LVGL v9's `lv_draw_sw_line` has no anti-aliasing — `draw_line_skew` writes per-pixel `opa` with no perpendicular coverage — so a diagonal road at 466 px is staircase by construction, and "screen-side supersampling" is not a flag but a rasteriser. Second, the real gap is capacity, not colour: `kMaxMapSceneRoads=24` / `kMaxMapSceneRoadPoints=192` is ~8 points per road, while the palette already receives six `MapRoadClass` values and uses three. So the plan is: casing first (zero cost, GTA maps do it anyway), then a coverage rasteriser for roads and route only (2× render-and-downscale rejected at 2.8 MB PSRAM and 870 k averaged pixels per frame), plus an area layer and the unused road classes, with buildings turned into a setting.

**`SettingsState` grew by two bytes, deliberately without a revision bump.** `ambient_night` is an observation rather than a preference — the device has no clock and no light sensor, so `Auto` has nothing else to resolve against — and `show_buildings` is the settings form of the readability decision. 14 → 16 bytes, `validate`/codec/`operator==` updated, both bytes refused as `InvalidEnum` above 1, and the 14-byte legacy shape refused as `TruncatedPayload` instead of being read as a settings set that is missing two fields. A `kPayloadRevision` bump was rejected because the payload revision is global: it would have rejected every other message in the same build, and both endpoints ship together in this project anyway. Golden `settings.payload` is now `01034101283c001f83ff010105000000`, and §6.12 of both protocol READMEs explains the break.

**The device half, end to end.** `DeviceSettings` is v2 with a 10-byte blob and decodes v1 in place — a flash that silently resets theme, units and lean scale is a regression the rider sees. `settings_store.{h,cpp}` owns `moto_device/settings` and initialises NVS on demand, because `app_main` now loads settings **before** `moto_nav_ui_create()` (the phone may not connect for minutes; booting into the vendor theme first would flash the wrong palette at every power cycle) while the BLE transport is the other `nvs_flash_init()` caller. The replay happens after `create()`, because the tree reset clears the theme and the building visibility. `PhoneNavBridge::consume_settings` gates on revision, persists only when the values actually changed, echoes with the applied revision at priority 0, and applies through the render task's new `RenderDisplay` flag so LVGL is never touched from the BLE callback. `moto_nav_ui_set_show_buildings()` hides the fill layer and the outline lines together — one of them alone leaves either a hollow ghost or a solid block with no edge — and the geometry update re-applies visibility when a new scene changes which footprint slots are in use.

**Two defects found while doing it, neither in the new code.** `device_settings.cpp` had never been in the ESP-IDF component's `SRCS`; it was only ever compiled by the native tests, so the firmware had been ignoring a file the tests pinned. And capability bit 10 is now advertised by the transport, which is what turns the phone's revision gate on.

**The test I wrote first was measuring the overlay.** `test_buildings_are_opt_in` failed with the layer provably visible, 466×466, and not hidden — because `create()` resets the link to offline and the subsequent `set_phone_connection(ONLINE)` raises the 920 ms `CONNECTED` confirmation, which is opaque and outranks guidance. The suite's own log had already recorded that trap; this time it presented as "the fill callback never runs". `pump(240)` before sampling fixed it. The test now asserts the default (hidden, outline too), the toggle, and that a repeated setting does not repaint.

**Verification.** Native **11/11**, `nav_ui_render_tests` **20 cases**, `scripts/verify.sh` green (todo log, native, Swift 41, iOS build). New bridge coverage: apply → persist → echo, replay ignored at an equal revision, AUTO resolved from the ambient flag, higher-revision-same-values echoing without rewriting NVS, and the boot replay reaching the UI with nothing to echo.

**State.** Working tree dirty, nothing committed. The phone half of Batch 2 is the next task and is written down as such: encode 0x17 in the ObjC++ bridge, gate on capability 10, keep a monotonic revision across reconnects, add the Auto / Day / Night control plus the buildings switch, and supply `ambient_night` from the phone's own sunrise/sunset because the device cannot compute it. The device is honest in the meantime: it advertises bit 10, runs defaults until a phone sends 0x17, and replays from NVS after that.

## 2026-10-23T01:12:04+09:00 — The firmware builds, three host failures deep

**`idf.py -C platforms/esp32 build` produces a binary for the first time in this tree.** `moto_gps_esp32.bin` is 0x13ff90 bytes against an 0x800000 app partition, 84% free, linked by xtensa-esp-elf 14.2.0 against ESP-IDF v5.5.5 and esptool 4.12.0. Only `x86_64` Linux had ever been asked to look at this code before, and every finding below is a thing that only the real target could report. Nothing was flashed.

**The session started on an error that was not true.** The bootstrap script stopped with `submodules still missing` for `lib_esp32c3_family` and `esp32c2-bt-lib`. Both submodules were fully fetched: `.git/modules/.../objects/pack` held the objects, HEAD pointed at the pinned commit, and `git submodule status` reported them clean — while the worktree was empty and the submodule's own index showed every file as a staged deletion. An interrupted `submodule update` leaves exactly that shape, so the script's readiness check, which looks at the worktree instead of trusting `status`, was right and the status flag was wrong. `git reset --hard HEAD` inside each one restored them from local objects with no network at all. `repair_submodules()` now runs before any remote is touched, because that is where one of the three runs was lost.

**Three host failures, each measured rather than guessed.** Espressif's Gitee mirror 403s for `esp32-wifi-lib`, so only its `esp32s3/` archives are pulled from jsDelivr and `submodule.<path>.update` is set to `none` so nothing drags it back. The toolchain CDN was the real cost: `dl.espressif.com` served 175 KB/s while `dl.espressif.cn` served the same file at 13 MB/s, a 75× difference on the same network, so the assets host is now probed with a ranged request instead of assumed. And `install.sh .` had pinned all twelve SoCs forever, because `idf_tools.py` merges the requested target against `idfInstalled.<path>.targets` in `idf-env.json`; clearing that list makes `esp32s3` mean esp32s3 and drops the RISC-V toolchain and its GDB entirely.

**The certificate failure is the one that is a property of this machine.** Two downloads died with `CERTIFICATE_VERIFY_FAILED: unable to get local issuer certificate`, and the reason is that this python.org 3.11 ships no CA file — `etc/openssl/cert.pem` simply does not exist — so `urllib` cannot verify anything. `idf_tools` hides this for `dl.espressif.com` and `github.com` by loading a pinned root into its own context, which is why the slow path appeared to work and the fast one did not. `SSL_CERT_FILE` pointing at the `certifi` bundle that was already installed fixed it; the script now sets it before installing.

**A third instance of a defect class this log already named.** The link failed on `moto::motion::sensor_to_bike` and `estimate_attitude`: `attitude_estimator.cpp` had never been in the ESP-IDF component's `SRCS`, so the firmware linked against declarations with no definitions. This is `device_settings.cpp` again — a `platforms/esp32/main/*.cpp` shared with the native tests and compiled only by `tests/native/CMakeLists.txt`, which is why `ctest` stayed 11/11 green while the firmware could not build. `scripts/verify.sh --no-ios` runs todo log, native, Swift and the iOS build; none of them is the xtensa target, so `idf.py build` is the only gate that covers this and the only reason it surfaced now.

**The lock file moved on its own and was kept.** The first successful build re-resolved four managed components into `dependencies.lock` — button 4.2.2, esp_lcd_co5300 2.2.0, libpng 1.6.59, qmi8658 2.0.2 — all inside their declared ranges. Committing that is the point of the file: it now names the versions that produced the binary, and a rebuild is a no-op instead of a silent upgrade appearing in the tree.

**Verification.** Native 11/11 (`ctest`, 1.5 s), `node scripts/todo_status.mjs --check` clean, `idf.py build` twice with the second a no-op, the bootstrap script re-run green in 8 s with every step skipped, and `git describe` back to a clean `v5.5.5` after the blocked-mirror `.gitmodules` patch was reverted (the proxy URL stays in `.git/config`, and `submodule_ready` is unaffected because `update = none` means nothing fetches it again).

**State.** Committed on `feat/gta-attitude`: `f72ea6a` the missing source, `38a4749` the lock, `346f4ca` the bootstrap script. The next step is no longer "can it build" — it is flashing it and watching the attitude path, the settings replay and the hidden-by-default building layer on the board.

## 2026-10-23T11:05:00+09:00 — The phone becomes the same poster as the handlebar

Started from a review of the round-screen work: the instrument was a poster, the phone was a well-behaved iOS form in roughly the same colours, and the two read as different products when a rider looks from one to the other.

**Round-instrument slice closed and committed.** The generated Barlow Condensed and Noto Sans SC faces are in the repository (9 faces, 13 MB of C plus the 8.1 MB OTF and its OFL licence); only `moto_font_nav_16.c` had ever been tracked, so a clean clone could not link once the CMake globs landed. The black-panel pixel probe came out of `app_main.cpp`, and `idf.py build` is green after the removal. The `DEF flush` half of that task turned out not to be a task: `managed_components/` and `sdkconfig` are both gitignored.

**Batch 2 phone half.** `DeviceSettingsBroadcast` plus `MotoBLESettingsStateInput` send `SettingsState` (`0x17`): fixed tables, local clamps, a navigation-must-stay-visible mask, capability bit 10, and a revision floored by wall-clock seconds so a reinstall cannot rewind below a device that still holds an older watermark. The golden check pins `settings.payload` — the one payload that grew without a wire revision bump. `AmbientNight` computes the sun's elevation from UTC and east-positive longitude (the zone offset cancels) and switches at civil twilight; dark mode at noon is not night.

**The palette became the instrument's.** `#141024` midnight purple, `#F6F0D7` road cream, `#F34DAA` band — the same values as `MapPalette` in `shared/nav_ui`. Orange is gone. `onAccent` is ink in both themes because cream on magenta is 2.8:1; the band is brand, not state, and does not change between themes.

**Every screen was rebuilt as a poster sheet.** Band header with a mono kicker and a heavy title, one hero word at 76 pt, numbered sections with the index in the display face, square panels and hairlines. Seven screens: 我的圆屏, 隐私与数据, 网关设置, 地图 (root + city search + city download), 路线, 骑行. The ride screen dropped from three equally weighted stat cells to one 76 pt distance plus a two-cell band beside the radar; the home wordmark went 88 → 132 pt and onto its own line with the radar at 300.

**Two defects only screenshots could find.** A view that applies `.motoTheme` itself and reads `motoPalette` from the environment gets the default palette — cream ink on cream paper, which looked like a disabled row. And `.navigationDestination` attached to the `NavigationStack` rather than its content makes the push silently do nothing; city search is a push now, because a sheet behind a sheet left the parent band peeking above the child's.

**Verification.** 46 app unit tests, 47 Swift package tests, native 11/11, `verify.sh` green on every push. Screenshots reviewed on iPhone 17 Pro (27.0) light and dark, iPhone SE (27.0) light, `sim_font_gate.sh` reporting 47,102 glyphs. Route preview is the one screen with no screenshot: it needs a configured gateway, and it carries its own open item in `docs/todo.md`.

**Not done.** The map downloads sheet ended up fully converted, so no stock `Form` or `List` remains in the app's own screens. `MotoMeter` is still dead code and its colour task was superseded rather than performed.

## 2026-10-23T12:02:00+09:00 — The lean page, the boot field, and the forty-hertz ghost

The review started on the panel, not on a native frame: the lean page was cream where the 03:10 poster was midnight purple, the navigation page was blank, and a left swipe did nothing. Those were four separate faults wearing one symptom.

**The boot screen was green because it took the map's ground, not the paper.** `moto_nav_ui_show_boot_screen()` used `kGtaDayPalette.background`, which is MAP GREEN; the comment above it said “cream stock”. The boot now asks for the persisted theme's `ground`, `ink` and `accent` through `moto_nav_ui_set_boot_theme()`, and `app_main` loads settings before the first frame. Day boots cream, Night boots midnight purple. The boot test pins both papers now, so a green field cannot come back unnoticed.

**Paging was compiled out of the firmware.** `config/lv_conf.h` overrides Kconfig through `LV_CONF_PATH`, and it never set `LV_USE_GESTURE_RECOGNITION`; the touch driver even logged “only single-point pointer events are available”. The native swipe test stayed green because `LV_USE_TEST`'s mouse indev synthesises `LV_EVENT_GESTURE` directly, which is exactly the blind spot. The shared config now sets the recognizer and the `LV_USE_FLOAT` it requires, with an `#error` guard in `gesture_event()` so neither build can quietly lose it again.

**The blank page was a missing cover, not a dead renderer.** The navigation page has no map without a phone, and the `CONNECT PHONE` lifecycle overlay started Hidden; `moto_nav_ui_create()` never called `refresh_lifecycle()`, so the first screen was a bare paper disc. The create path now paints the default offline cover. Separately, the first UI flush used to race NimBLE init and die with `recycle spi transactions failed` / `Draw bitmap failed`, leaving the boot frame on screen with no retry; the first frame is now flushed with `lv_refr_now()` under the LVGL lock before BLE starts.

**The lag was one `lv_obj_invalidate` in the empty-building branch.** `update_building_geometry()` cleared and invalidated the full-canvas `nav_building_fill` on every call, even when the previous snapshot also had no buildings. `present_motion` calls it at 40 Hz, so the idle navigation page was repainting 466×466 forty times a second: 3.3 fps, 296 ms render, and every swipe felt dead. It now invalidates only when the previous snapshot actually had buildings to clear. The serial log after the fix shows one startup perf line and then silence.

**The lean page itself was restored to the poster.** The band moved from 207–217 to 212–222, the casing to 197–212, the ticks and zero bar followed, and the horizon plus sun are painted even when the IMU has no sample; the no-IMU state keeps the composition and puts `NO IMU` on a plate. The 176 px `--` was two solid bricks, so the centre is simply empty until there is data. `test_lean_page_without_attitude_keeps_the_poster` pins the new state.

**Verification and state.** Native 11/11, `scripts/verify.sh` green, firmware `0x333e30` bytes (60% of the 8 MB app partition free), flash verified. Boot log: `loaded settings v2 theme=3 buildings=1`, no gesture warning, no SPI DMA error, no repeated display-perf line while idle. The 0-100 page is still the second page (`nav → 0-100 → lean → speed → compass → music`) and was never removed; it was unreachable because paging was dead and the UI was saturated. The six-page hardware visual pass remains open.

## 2026-10-23T12:32:00+09:00 — v2 P0: lean estimator, mount basis, and the UI quick wins

Started the v2 development guide on branch `feat/gta-attitude`. Baseline before
the first edit: native 11/11 green, working tree clean apart from an untracked
`platforms/esp32/sdkconfig.release.bak`.

**The measurement, not the drawing, was wrong.** v1 computed Lean as
`atan2(-g.x, g.z)` from a low-passed accelerometer vector. In a steady corner
the specific force is aligned with the bike's own vertical axis, so the more
settled the corner, the smaller the number it reported. `LeanEstimator`
(`platforms/esp32/main/lean_estimator.{hpp,cpp}`) replaces it: the gyro
integrates the world-up vector in the body frame, and three slow observations
correct it — kinematic (`atan(-v·psi_dot/g)` plus force alignment, 1.5 s),
static (full up vector, 0.5 s, the only pitch observer) and a straight-line zero
pseudo-observation (3 s). Bias learns only after 2 s stationary (τ = 10 s) and is
frozen while moving. Twelve synthetic tests cover it; the one that matters is
`test_coordinated_turn`, where a 40° steady corner built from physically
consistent inputs (`a_y = 0`, `a_z = g/cos40`) must read 40 ± 2°. That case is
the regression v1 could not pass. `test_hang_off` pins 35° frame lean against
42° effective lean.

**The bracket became a rotation.** `MountCalibration` captures the sensor-frame
up axis from 2 s of stationary samples and the forward axis from one straight
acceleration, then maps every sample through that fixed basis. This replaces
`lean_zero_offset_cdeg`, which could only trim a zero and — as the earlier
audit found — was never passed to the estimator at all (`motion_heading_sensor.cpp`
constructed `AttitudeCalibration{}`). The adapter now builds the assumed bracket
basis (sensor +y up, −z forward) with `CAL_OK = false`, runs one estimator, and
drops the old yaw-bias calibration loop.

**Range and UI.** `NavPresenter` clamps Lean to ±70° and sets the new
`lean_saturated` flag. `ui.perf_delta` is now created — the page's state machine
had been writing `PB`/delta strings into a null label since the 0-100 page was
introduced. BEST and delta share one row; the speed-limit sign is one component
used by navigation (61) and speed (52). `update_nav_status()` reads palette
tokens instead of `kAmber/kGreen/kQuiet/kWhite`, so theme switching no longer
leaves old literals behind. `DESIGN.md:273` casing corrected to 197–212.

**Docs.** `docs/adr/0003-lean-angle-definition.md` records the definition,
sign, range, source priority, bias rule and the retirement of the scalar
offset. Task log entries are in `docs/todo.md`.

**Verification: native 12/12** (`lean_estimator_tests` added; render suite
still green with the perf-page layout change). Firmware build is the remaining
gate for the new translation unit; the 125 Hz polling path and the missing
speed feed are recorded as open v2 tasks rather than guessed at.

## 2026-10-23T12:56:00+09:00 — v2 step 1: static calibration is runnable; estimator guards from the review

The review reordered the plan: calibration first, then the acquisition chain,
then speed. That order is now reflected in the code — the estimator's biggest
error source was not its corrections but a bracket basis that did not exist.

**Calibration path, end to end.** A long press on the Lean page (anywhere; a
silent full-canvas layer named `moto-lean-calibration`) calls the sensor's
`request_static_calibration()`. The sensor task captures 2 s, requires a quiet
window, and reports back; `app_main` persists the basis to NVS `att.rmount` via
the new versioned 17-byte blob (1e-4 components, trim in cdeg, CRC16). The basis
is loaded before the sensor task starts, so no sample is ever mapped twice
through different frames. Step 2 is yaw-only and guarded: ±10°, two agreeing
0.5 s segments, rejection otherwise.

**The capture no longer assumes a perfect accelerometer.** The old gate wanted
`|‖a‖−g| < 0.02 g`, which a 30–50 mg zero-g offset can fail forever. The
capture learns its own stationary magnitude from the first sample and then only
demands quietness. Trim is ±5° about the forward axis, applied inside
`to_body()` so it moves Lean and nothing else.

**Estimator corrections from the review.** The IMU rides h above the roll axis,
so the measured lateral specific force contains `h·φ̈` and the vertical one
`h·φ̇²`; both are now removed before any observation, using the gyro roll rate
and a 20 ms derivative filter, and the kinematic gain additionally drops when
`|φ̈| > 300 °/s²`. A 12° flick test shows 0.9° worst error with the term and
2.3° without. With no speed, "stationary" now also requires low per-sample
acceleration jitter, and bias learning waits 3 s instead of 2 s.

**Geometry probe found two shipping defects.** A temporary inside-the-disc scan
showed `ui.nav_home` at canvas x 432–491 — off the 466 canvas and outside the
disc, so the home shortcut never rendered — and page dots in a straight row
whose outer items sat at r=231, outside the glass. The shortcut moved to
px(206,262) (worst corner r=191) and the dots now follow the casing arc at
r=203. The probe also listed five remaining violations (nav status label box,
maneuver icon box, nav limit child, 0-100 strip ends, one Lean element); the
gate is logged as a task rather than committed red, and it still needs a rule
for whether a centred label's box counts as furniture.

**Verification:** native 13/13 (new `mount_basis_tests`, plus calibration and
lever-arm cases in `lean_estimator_tests`), `idf.py build` green at 0x335650
(60% of the app partition free). Still not verified: the H1 tilt table, the
rider-facing countdown/CAL status, and step 2 on a real ride.

## 2026-10-23T13:12:00+09:00 — Review r1: the learned magnitude becomes the static gate

The review's first code note was right and cheap: the capture learned the
unit's stationary magnitude from its **first sample**. `MountCalibration` now
stores the mean of the accepted 2 s window, `is_static` compares `|a|` against
that value instead of 9.80665, and the value slow-updates (τ=10 s) only inside
a stationary window. It rides in `att.rmount`, which grew from 17 to 19 bytes
(version 2, magnitude in 0.01 m/s²). A test now feeds 1.04 g: the learned
gate reads it as stationary, the nominal gate never does.

Also written: `docs/DESIGN_V2-r1.md`, the reviewable revision draft. It maps
every checklist item to a section, switches the pseudocode to the gravity-vector
form actually implemented (`ψ̇ = ω·û`, `φ = atan2(û_y, û_z)`), documents the
lever-arm and φ̈ gate, tightens the no-speed ZUPT, freezes heading at a
standstill, moves HOLD to moving-time only, replaces the partition plan with
4 MB×2, adds round-trip time sync, adds the geometry-gate rules with a
worst-case string table, and marks the vibration thresholds as temporary
pending H2. It is deliberately **not committed** yet: the reviewer asked to
check it item by item first.

Verification: native 13/13, `idf.py build` green at 0x335760 (60% free).

## 2026-10-23T13:40:00+09:00 — Step 2: telemetry format, deterministic replay, size gate

Two of the four step-2 items are real; the other two are blocked on hardware
facts the repository does not contain, and were not invented.

**Record and replay.** `dyn_record.{hpp,cpp}` defines the development stream:
a 24-byte header, then `type + fixed payload` records for 112 Hz raw IMU plus
the estimator output, the phone GPS frame as received (with receive time), and
the mount basis in force. Every field is packed by hand little-endian, so the
same bytes are written on the ESP32 and replayed on the host. `dyn_replay`
feeds them through the same `LeanEstimator` and prints fixed-precision CSV;
`moto_replay <file>` is the CLI. The test replays a generated file twice and
compares strings, and separately proves that changing one gyro byte changes the
output — a replay that echoed the recorded estimator values would fail.

**dt independence.** The 40° coordinated-turn case now runs at both 112 Hz and
125 Hz: both must land within 2° of truth and within 1° of each other.

**App budget.** `scripts/check_app_size.sh` enforces 90% of the planned 4 MiB
slot and runs inside `verify.sh`. Current image is 3,364,704 B = 80%, about
0.64 MiB free, which matches the review's estimate. The partition table itself
is unchanged until fonts and resources move out of the app.

**Not done, with reasons.** FIFO: the vendored QMI8658 component exposes only
polling — no FIFO registers or CTRL9 command anywhere in its API or sources,
and the repo has no datasheet register map. A guessed sequence is how hardware
bugs are made, so the sensor task stays on 125 Hz polling and the file header
records that rate. Telemetry sink: `BSP_CAPS_SDCARD` is 0, the BSP has no
`bsp_sdcard_mount()`, no SDMMC init and no card-detect pin, so "write to SD"
needs the board's pin assignment (schematic) or a 4 MB `tlog` flash partition
instead. CAL feedback and the 2 s/3 s long-press split are specified in r1 and
land in the next UI slice.

Verification: native 14/14, verify.sh green including the new app-size gate,
`idf.py build` green.

## 2026-10-23T14:05:00+09:00 — 32 MB storage plan, tlog recorder, and the board report

The SKU changed the storage assumptions: 33691 is a 1.75C with 32 MB flash, no
TF slot and no GNSS. `partitions.csv` is re-cut accordingly — nvs left at
0x9000/0x6000 so `att.rmount` survives, app0/app1 6 MB each, assets 3 MB,
coredump 64 KB, and a 16 MB `tlog` starting at 0x1000000. The size gate now
enforces 6 MiB × 90%; the image is 3.37 MB, 46% of the slot free. Changing the
table still requires a USB full flash, which the partition file and TODO say
explicitly.

`TlogLogger` writes the dyn_record stream to `tlog`: 4 KB sector writes, 2 s
flush, append position in NVS, one-time above-16-MB erase/write/read self-test,
capacity stop instead of a wrap (16 MB ≈ 74 min at 3.6 KB/s). It is fed from
the IMU task with raw accel/gyro/temperature plus the estimator output, and the
PWR path flushes the partial sector before deep sleep. `flash_guard` exists now
so the FIFO read window and flash erase cannot interleave when FIFO lands.

The boot report is code, not results: one log line with the real flash size,
then an I2C scan of 0x08–0x77 naming the expected addresses. r1 §1.1 stays
unwritten until the board is flashed and the scan is pasted back — the 1.75C
RTC question is exactly the kind of thing this repo has learned not to assume.

One correction to the review: nvs in the current table is 24 KB
(0x9000 + 0x6000), not 64 KB. Growing it would overlap the old phy_init page;
keeping offset and size is what actually preserves the calibration blob, so
that is what shipped and what r1 §11.3 now says.

Not in this slice: FIFO (register map supplied, implemented next), CAL feedback
UI, and the r1 full merge. Verified: native 14/14, verify.sh green, idf.py
build green against the new partition table.

## 2026-10-23T15:20:00+09:00 — Step 2 complete in code: self-describing tlog, FIFO, CAL UI, r1

**tlog is now self-describing.** 4 KiB blocks with magic/version/seq/boot id/
first sample/payload length/CRC32, payload written first and the header last,
so a torn block is skipped rather than trusted. Each boot's first block carries
a SessionMeta record (mode, ODR, sample rate, h, estimator gates, firmware
hash, mount), and a FIFO fallback appends a fresh one. The append point is the
first all-0xFF sector found by binary search — `tlog.pos` is gone from NVS,
which also keeps wear off the 24 KB partition that holds `att.rmount`. The
partition stops when full and the lean slot reports `LOG FULL`; `moto_replay`
takes the raw `parttool` image, validates every block CRC and prints a session
boundary per boot.

**FIFO per the supplied register map.** CTRL1 0x70 / CTRL2 0x24 / CTRL3 0x54 /
CTRL5 0x99 (read back and logged) / CTRL7 0x03 / CTRL8 0x80, Stream-128 at
watermark 4, REQ_FIFO→ACK via STATUSINT, read window inside flash_guard, 4-frame
average to ~112 Hz, TIMESTAMP-based loss accounting, overflow count and
window max/mean. A fallback is visible everywhere it matters: serial reason,
`kLeanImuFallback`, session mode 0, and `IMU POLL` on the lean page. The
runtime switch is NVS `imu.fifo` plus `imu fifo on|off`; `imu stats` and
`imu lean` are the diagnostics. None of this has touched a board yet.

**CAL UI per r1.** Press starts a timer, the ring appears after 300 ms, a
2–3 s release clears peaks, ≥3 s release runs a 3 s countdown and then asks
the sensor to capture. Results are posted as atomics and applied on the LVGL
task, so the sensor task never touches widgets. `CAL`, the countdown, OK,
FAIL MOVING/RANGE, `IMU POLL` and `LOG FULL` share one slot at (0,+130).
Render tests drive real press/release events with a 5 ms simulated tick.

**Pitch sign fixed.** The review was right: `asin(−û_x)` reported nose-down as
positive. It is now `asin(+û_x)`, and the longitudinal-G term flipped with it.
The test feeds the physically correct nose-up specific force, not the one that
matched the old sign.

**Docs.** `docs/DESIGN_V2.md` replaces the draft and the r1 delta; `DESIGN.md`
now says it is superseded. `docs/BENCH_TEST_PLAN.md` is the ordered bench
checklist with commands, serial keywords and pass criteria, including the
three H1 groups and the replay/error-table step.

**Verification:** native 14/14, verify.sh green, `idf.py build` green at
0x338f80 (46% of the 6 MB slot free). Still unverified: every hardware claim
in the bench plan.

## 2026-10-23T16:20:00+09:00 — Latch polling replaces the FIFO window; tlog stops erasing

**The decision was to stop relying on the read window, not to tune it.** The
16:05 measurement said the FIFO lost 10–12% of samples because a 1.36–1.39 ms
read window recurs every 8.9 ms, and TIMESTAMP reads 0 on this unit so the loss
had to be inferred from timing anyway. `qmi8658_latch.{hpp,cpp}` therefore
reads the QMI8658B latch every 4464 µs — two ODR periods, 224.2 Hz — while the
sensor keeps sampling at 448.4 Hz behind its own LPF. The STATUSINT handshake
(Avail, then Locked within 12 µs, three retries) is what makes the 14-byte
TEMP + six-axis burst one instant, so a busy bus no longer throws a sample
away; the two-sample average then decimates to ≈112 Hz exactly where the FIFO's
4-frame average used to sit.

**Configuration is explicit and read back.** Soft reset (0x60 = 0xB0) first,
because the IMU stays powered across an ESP32 reset and a previous FIFO session
can leave FIFO_CTRL in Stream mode where the output registers and the sync
status are hidden. Then CTRL7 off, CTRL1 = 0x40 (auto-increment, little-endian),
CTRL2 = 0x24 (±8 g at 448.4 Hz), CTRL3 = 0x54 (±512 dps, same ODR), CTRL5 = 0x55
(both LPFs, mode 10), CTRL8 = 0x80 so CTRL9 completion arrives on STATUSINT
(the IMU INT net is not routed on this board), CTRL9 = 0x12 with CAL1_L = 0x01
to disable AHB clock gating per §11.2.1, a bounded 50 ms poll for CmdDone, the
ACK, and only then CTRL7 = 0x83 to sync accel and gyro. Every register is read
back and logged; the handshake is the one step that fails into a silent
fallback, so it is also the first thing the bench card checks.

**Gaps are attributed, not just counted.** `flash_guard_take(0)` failing is
`flash_skips`; an interval longer than two periods is `flash_gaps` when
`note_flash_blocking()` marked a tlog page write inside it and `missed_reads`
otherwise, which is the distinction §3.2 asks for. `avail_zero`, `lock_waits`,
`lock_timeouts`, `i2c_errors`, `invalid_samples` (a magnitude outside 0.1–8 g)
and interval max/mean complete the set. Eight consecutive I2C errors or twenty
consecutive lock timeouts falls back to polling, with the reason in the serial
log, `kLeanImuFallback`, the session meta and the lean plate. `load_imu_fifo_enabled()`
now returns off when NVS has no value, so latch is what a fresh unit runs and
`imu fifo on|off` selects the FIFO only for the A/B.

**The health data is on disk.** `ImuStatsRecord` (32 B, type 5) is appended
every 10 s and on every mode change, and `moto_replay` prints it as `# imu ...`
so a dump can be audited for acquisition loss without a serial capture. The
session record's mode byte is now 0 poll / 1 FIFO / 2 latch, carried by the same
number the sensor task reports.

**tlog never erases at runtime.** A sector is checked with 256-byte reads
against 0xFF before the append; if it is dirty, recording stops, `dirty_` is
set and the log names the `parttool.py erase_partition` command. That removes
the cache-off erase that used to freeze the IMU bus mid-ride, at the cost of a
host step — the partition is now a prerequisite, not something firmware
recycles. On the lean plate `LOG DIRTY` outranks `LOG FULL`, which outranks
`IMU POLL`.

**Display and bench instrumentation.** QSPI went 40 → 30 MHz after the lean page
produced `DMA TX underflow detected` while LVGL drew from PSRAM; the draw
buffers are now full height (466 rows, still two PSRAM buffers). The display
timing window can be logged on demand and carries monotonic totals, `ui dbg`
reports per-frame ms for the sun, horizon and hero text with a hide mask that
isolates each layer, and `sys tasks` prints task state and stack high-water for
the sensor, render, BLE, power and demo tasks.

**The geometry gate is committed.** `moto_nav_ui_page_root()` lets the test walk
each page's container; `ui_geometry_manifest.txt` records box, colours,
opacity, radius and scroll/click/gesture flags keyed by child-index path, so a
moved box or a revived scroll flag shows up as a reviewable diff. It registers
the boxes; the r≤195 assertion is still open.

**README.** The Xuebao relay advert at the top of both READMEs is gone, along
with the five `assets/brand/xuebao-*` files only it referenced. The black-and-
white cover and wordmark are untouched.

**Verification:** native 14/14 and `scripts/verify.sh` green (also through the
pre-push hook); `idf.py build` green at 0x33d1b0, 46% of the 6 MB slot free.
Not verified: every claim above that needs a sensor, starting with the AHB
gating handshake.
