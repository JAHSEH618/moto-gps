> **Superseded:** the handlebar screen design is now specified by `docs/DESIGN_V2.md` (v2.0-r1).
> The device chapters below are kept for history; where they disagree, v2 wins.

# Design

Visual system for the MOTO GPS phone app (`platforms/ios`) and the web shell (`platforms/web/shell`). Three themes, one choice: the rider picks a theme on the phone, and the phone and the round screen both wear it.

## Theme switch

| Theme | Round screen | Phone | Picked as |
|---|---|---|---|
| **Original** | the upstream interface this project was forked from (`moto_nav_ui_classic.cpp`): true black, white route over grey roads, Montserrat, four pages | `MotoThemeName.original`: black, warm white ink, white accent and route, system type, no texture | 原版 |
| **Liberty Night** | GTA III, black paper, white band | `MotoThemeName.night` | GTA · 夜间, or GTA · 自动 after dark |
| **Liberty Day** | GTA III, newsprint paper, black band | `MotoThemeName.day` | GTA · 白天, or GTA · 自动 in daylight |

The control is one flat list of four choices — **GTA · 自动 / GTA · 白天 / GTA · 夜间 / 原版** — on its own sheet, opened from **MENU → 02 主题** on the home screen (one level under the menu, not inside the device sheet). It is persisted on the phone, which applies its own look at once and sends `SettingsState.theme` to the handlebar (now, or on the next handshake if the link is down). There is no second, phone-only appearance switch: one choice, two surfaces, so a rider never has to set the look twice. The phone no longer follows the system's dark or light mode.

Original is not a palette of the GTA tree. It is a separate widget tree with its own layout and page set; the round screen tears one tree down and builds the other when the rider crosses between Original and GTA, and replays the current page, link state, track and snapshot so the switch reads as a theme change. A request for the Performance or Lean page lands on navigation in Original, which never had those pages.

`Auto` needs a light condition the handlebar does not have: there is no clock, light sensor or network time on the ESP32-S3, so the phone supplies one day/night flag with the settings and the device resolves `Auto` to `Day` or `Night` from it (day until the first flag arrives). The phone resolves its own look from the same flag (`DeviceSettingsTheme.phoneLook(ambientNight:)`), so the two surfaces cannot disagree. An explicit `Day` or `Night` ignores the flag.

The two GTA themes share geometry, spacing, type scale, and the circle motif. Only paper band, accent, display weight, and texture change — never structure. If a component needs a different layout in the other theme, the component is wrong.

**Liberty Day is a light theme on purpose.** A dark interface behind glass in direct sun becomes a mirror: reflectance wins and the numerals disappear. Readability in daylight requires a high-luminance paper and near-black ink, which is also the palette of the GTA III cover's white field. Atmosphere is sacrificed for legibility; that is the trade.

## Color

The GTA themes are GTA III's Liberty City (2001): the cover's black field and
white panel gutters, and the HUD's own colours — money steel-blue, health
orange, armour olive — each lifted (night) or darkened (day) until it clears AA
as text. The phone's accent is **neon pink**, the handlebar band's `#F34DAA`,
not GTA III's clock gold: one accent on both surfaces, and no gold anywhere on
the phone (2026-10-26, rider's call). All values are exact; no gradients on data surfaces. Phone
and round screen share the tokens (`MotoPalette.night/.day`,
`kGtaNightPalette/kGtaDayPalette`).

### Liberty Night

| Token | Value | Use |
|---|---|---|
| `bg` | `#0A0A0A` | app background, round-screen paper |
| `surface` / `surface-2` | `#141414` / `#1D1D1D` | panels, raised rows, cover-grid tiles |
| `line` | `#333333` | 1px rules |
| `ink` | `#F2EEE4` | box-art white: text |
| `ink-muted` | `#A8A294` | secondary text, labels |
| `hud` | `#F34DAA` | HUD numerals (neon pink, the handlebar band), with a hard 3pt black drop |
| `accent` | `#F34DAA` | primary button, active selection |
| `route` | `#F08A3C` | route stroke, rider markers on the round screen (health orange) |
| `link` | `#86A6CF` | BLE link, GPS fix (money steel-blue) |
| `ok` | `#A5B97A` | completed (armour olive) |
| `abort` | `#F0614A` | aborted, off-route |
| `band` | `#F2EEE4` | sheet headers, round-screen rim: the cover's white gutter |
| map | land `#3A3A3A`, blocks `#222222`/`#1B1B1B`, water `#14202E`, park `#28311F` | radar |

### Liberty Day

| Token | Value | Use |
|---|---|---|
| `bg` | `#EFECE4` | newsprint paper |
| `surface` / `surface-2` | `#E4E0D6` / `#D8D3C7` | panels, tiles |
| `line` | `ink @ 24%` | 1px rules — heavier so they survive glare |
| `ink` / `hud` | `#111111` | text and numerals; no drop shadow by day |
| `ink-muted` | `#4B463E` | secondary text only, never below 15pt |
| `accent` | `#AA1068` | the band's pink at text strength: works as text (6.0:1, 4.7 on `surface-2`) and as a fill under paper type |
| `route` | `#B04716` (phone) / `#F08A3C` (round-screen map, on grey) | route |
| `link` | `#35567F` | BLE link, GPS fix |
| `ok` | `#4A5C2A` | completed |
| `abort` | `#A8281C` | aborted |
| `band` | `#111111` | sheet headers, rim: the logo panel's black |
| map | land `#C9C5BB`, blocks `#8F8B83`/`#9C988F`, water `#6E879F`, park `#7D8A5B` | radar |

**The band is brand, not state.** It never signals a warning. Marks drawn on the
round-screen band (progress, speed sweep, protractor ticks) use the band's own
ink (`on_accent`), so they read on white by night and on black by day; the rider
point, compass index and zero bar are HUD orange, which reads on band and casing
in both themes.

### Contrast (measured, sRGB relative luminance)

| Pair | Ratio | Level |
|---|---|---|
| Night `ink` on `bg` | 17.1:1 | AAA |
| Night `ink-muted` on `bg` | 7.8:1 | AAA |
| Night `hud`/`accent` on `bg` | 6.1:1 | AA |
| Night `route` / `link` on `bg` | 7.9:1 | AAA |
| Night `abort` on `bg` | 6.1:1 | AA |
| Night `onAccent` on `accent` | 6.1:1 | AA |
| Day `ink` on `bg` | 16.0:1 | AAA |
| Day `ink-muted` on `bg` | 7.9:1 | AAA |
| Day `accent` on `bg` (and paper on `accent`) | 6.0:1 | AA |
| Day `route` on `bg` | 4.7:1 | AA |
| Day `link` on `bg` | 6.4:1 | AA |
| Day `abort` on `bg` | 6.0:1 | AA |
| Both: `on-slab` on `slab` | 17.5:1 | AAA |

`ink-muted` is never used for data in either theme; it is for labels and secondary
prose, and never on the slab — the slab has its own `on-slab` token for that.

## Typography

Two families plus the system mono. No italic anywhere in headings, labels, or data.

| Role | Family | Spec |
|---|---|---|
| Display numerals | Barlow Condensed (bundled, OFL) | 800 in both GTA themes, tracking `-0.01em`, tabular, HUD drop by night |
| Labels, units, meta | SF Pro Expanded Heavy in GTA themes (the HUD's wide square caps; SF Mono in Original) / `ui-monospace` (web) | caps, tracking `+0.16em` |
| Prose, Chinese | PingFang SC (iOS) / Noto Sans SC (web) | 400/600 |
| Buttons | PingFang SC 600 or Barlow Condensed 600 caps | — |

### Scale (semantic, Dynamic Type capable)

Display roles use the bundled condensed face laid on `.largeTitle` so they scale, then capped by the host view. Text roles map onto HIG text styles, which keeps Dynamic Type, Bold Text, and the 11pt iOS minimum intact.

| Token | Default | Source | Use |
|---|---|---|---|
| `display-xl` | 76 | Barlow Condensed `relativeTo: .largeTitle` | remaining distance, speed |
| `display-lg` | 56 | same | run timer, ETA |
| `display-md` | 34 | same | panel headline numbers |
| `title` | 22 | `.title2` semibold | screen title |
| `body` | 17 | `.body` | prose (HIG default iOS size) |
| `secondary` | 15 | `.subheadline` | metadata |
| `label` | 12 | `.caption` mono medium | caps labels |
| `micro` | 11 | `.caption2` mono medium | caps meta in the status band (HIG minimum) |

Display tracking floor is `-0.02em`. Anything tighter is a defect. `Font.fixedSize:` is banned — it bypasses Dynamic Type.

## Layout

- Spacing scale: `4 · 8 · 12 · 18 · 27 · 40 · 64`. Rhythm comes from jumping two steps, not from uniform gaps.
- Radii: `0` (panels, sheets, buttons, bars), `2` (chips, inputs), `999` (dots and the live indicator only).
- No shadows anywhere. Elevation is expressed by `surface` steps plus a 1px rule.
- **Status band**: 54pt + safe area, 1px bottom rule, three cells (`LINK` / `FIX` / `ETA|CLOCK`), mono caps 10.
- **Cover**: the home hero *is* GTA III's box art — framed panels inside 5pt `band` gutters, edge to edge: the radar panel, a fixed 132pt side strip holding the MOTO / GPS logo block (Anton, `on-slab` on `slab`) and the safehouse panel, then a wide panel for GO and 出发. The command bar sits under the cover on plain paper; no other screen carries texture. Pricedown (the GTA logo face) is not used: Typodermic's free licence excludes app and device embedding.
- **Safehouse blip**: HUD-orange house (eaves and a door, the rider arrow's size; white in Original) on a black casing at home's real bearing — north-up, 1.5 km to the rim on the home radar; in the route projection on the ride radar; on the round screen from the route window or, with no route, from the latest map window's centre, above the waiting screen (Original: on its map strip, under a route only). A home past the rim keeps its bearing and rides the rim.

## Circle motif

The handlebar screen is round; the phone repeats it at three sizes.

| Size | Where | Content |
|---|---|---|
| 248 | Home hero | poster street abstraction + sweep, `N` badge on the rim |
| 176 | Active ride | the real route window, streets and footprints around the rider |
| 44 | Status band | connection state ring |

Radar chrome (GTA III radar): **heavy black bezel** (2.8% of the diameter) with a 1.5pt `band` line inside it, rider as a chunky outlined arrow, route stroke in `route` with a 1pt `bg` casing. The abstract hero map is a city, not a grid of lines: land is the road colour and blocks (two tones, some split into lots) sit on it, so streets are the gaps; avenues every fourth street are wider; a river with a curved shoreline and one bridge, and one park. With real data, roads are `map-land` on a `map-block-2` casing over block ground; on the round screen every road sits on a 3px dark casing layer. Devices that ride on the ring and never float over the map:

| Device | Spec |
|---|---|
| Safehouse blip | `route` house with eaves and a door on a black casing, at home's bearing; past the rim it rides the rim. Present only when home is set. |
| Compass badge | Circular cream badge (the speed-sign face) with a black `N` and edge in both themes, so it reads on the black casing by day too; it rides the rim at north's bearing. The turn glyph and distance stay inside that ring. |
| Progress ring | **The ring is the gauge.** The completed fraction of `route_progress_m / total_distance_m` replaces that segment of the 2px rim in `accent`, starting at twelve o'clock. No second track ring: a separate arc read as a halo. Real data only; never repurposed as a health bar. |
| Counter bar | `ink` slab with `onSlab` numerals in a corner, the way GTA parks cash. |

Map fill inside the circle: `surface` base, park at `ok`, water at `link`, roads and blocks in `ink`. Strength is per theme because dark-on-cream at 10% disappears while light-on-black at 10% reads:

| Layer | Vice | Heat |
|---|---|---|
| Blocks / footprints | `ink` 10% | `ink` 17% (abstraction) · 10% (real footprints) |
| Roads | `ink` 24% (abstraction) · 55% (real) | `ink` 62% (abstraction) · 34% (real) |
| Park / water | 24% / 22% | 34% / 30% |

The home radar draws a poster abstraction — a city that is clearly not a claim about a location. The ride radar draws the same real window and route the handlebar gets: a live route over invented streets is a lie, so real geometry replaces the grid as soon as a scene exists.

No texture inside the circle in either GTA theme — GTA III's radar is flat, and thin bands turn into haze in glare. Texture never overlays the rider dot or route.

### Poster devices (what makes it read as GTA, not as "clean dark UI")

| Device | Rule |
|---|---|
| Wordmark | One huge condensed Latin word in `accent` at 88pt (`GO`), outlined in `slab` with a hard offset drop, Chinese verb at `.title2` beside it. Never a small title bar. |
| Command bar | A solid `accent` block, 56pt tall, `.secondary` mono text and one magnifier. The only filled surface on a screen; no trailing chip, the whole bar is the control. |
| Menu rows | Numbered pause-screen list: `01…05` in condensed 34pt, title at `.title3`, 1px rule under every row. Numbers are real order, not decoration. |
| Section head | 14×3pt accent tick + 11pt mono caps. No sentence-case section titles. |
| Rules | 1px default, 2px after a block that ends a group. Never 0.5px, never shadowed. |
| Map content | Radar draws blocks and streets in `ink` at 10–22%: a minimap silhouette, not a blank disc. |
| Rider | A chunky arrow, not a dot. Heading is part of the readout. |
| Dusk skyline (round screen) | Liberty City at dusk along the bottom of the launch, compass, lean and music pages and the waiting screen: two rows of towers, far row lighter, neon pink lamps by night, pale print greys with paper windows by day. Frame, not data: it sits between the paper disc and the casing and below every reading, and is static. Not on navigation (the map is the world). |
| HUD labels (round screen) | State labels (`LAUNCH`, `L`/`R` maxima, `CAL`, `NO IMU`, music source) are cased HUD type with no box, as GTA III's HUD never boxes a label. Counter slabs (`BEST`, `KM/H`) keep their block. |
| Boot (round screen) | A GTA III loading screen in hard cuts, no fades: bezel black, then the box-art panel grid (radar with route and rider, safehouse, the city under a HUD-orange dusk sky) at 80 ms, the cream MOTO / GPS logo cased in black with a hard drop at 380 ms, then a neon pink loading bar stepping eight times from 520 ms. It measures the 1.25 s boot hold and claims nothing else. |
| Link cover (round screen) | The waiting screens (CONNECT / LINKING / LINKED / READY TO RIDE / BUILDING ROUTE) are the same box art: a 6 px `band` gutter frames three panels — GTA III's pager (black handset, lit green LCD carrying the Chinese instruction, kicker `PHONE LINK` / `READY` / `ROUTE` above it), the two-line headline on the paper, and the city under a HUD-orange dusk sky. A pager cursor blinks while the cover waits on the rider; the boot's pink bar steps (eight steps, a beat full, again) while it waits on the system. `PHONE LINKED` turns neon pink by night. The pager and dusk panel keep their colours in both themes. |

## Reference grammar (what we copied, and from where)

Source: `MickMelon/gta-sa-hud-react` (MIT code, proprietary fonts), `Geckomacabre/GTA-VI-Inspired-HUD-for-FiveM` (FiveM resource, mixed licences). Used as **visual evidence only** — no code, fonts, or art copied.

| Observed device | Source | What we took |
|---|---|---|
| HUD type sits directly on the world, outlined 2–4px black, zero containers | SA HUD React | Display numerals get an ink outline when they overlay the map; no card behind them |
| Status bars: 2px black frame, flat fill, no radius | SA HUD React | `MotoMeter` |
| Zero-padded numbers (`$000001337`) | SA HUD React | Pad distance/time numerals on the ride screen |
| Two-tier turn card: quiet strip + heavy panel + solid arrow tile | GTA VI HUD | `MotoInstructionCard` — tile is `route` magenta, not `accent` |
| Route line drawn in magenta, white rider arrow | GTA VI HUD | `route` token; `RiderArrow` |
| Speed-limit sign: white, 3px black border, tiny caps + huge numeral | GTA VI HUD | `MotoSpeedSign` |
| Minimap framed by a thick dark ring, blips colour-coded by meaning | GTA VI HUD | 2px ink radar ring + buildings in the disc |

**Not copied:** Pricedown, Chalet London Nineteen Sixty, Aharoni — all proprietary faces. Our condensed display face is Barlow Condensed (OFL), and the outline technique is reproduced with stacked offsets rather than those fonts.

## Components

| Component | Spec |
|---|---|
| Primary button | `accent` fill, `bg` text, radius 0, 52pt tall, caps 600, no shadow |
| Secondary button | transparent, 1px `line`, `ink` text |
| Command bar | 56pt, 1px `line`, radius 2, leading glyph, `SET DESTINATION` in mono caps, trailing ↵ badge |
| List row | 1px `line` bottom rule, 64pt min, leading mono index, no chevron box |
| Stat cell | label above, display numeral below, unit in lowercase mono at the numeral's baseline, 1px rule between cells |
| Sheet | `.presentationCornerRadius(2)`, hard 1px top rule, `surface` background |
| Segmented | 1px `line` container, selected cell = `accent` fill + `bg` text |
| Empty state | glyph + one sentence of instruction; never a system illustration |
| Loading | 1px progress rule that grows left→right; no spinners in content |

Every interactive component ships: default · pressed · focused · disabled · loading · error · success. Pressed states use a `surface-2` fill, never a scale transform.

## Motion

| Motion | Spec |
|---|---|
| Radar sweep | 4s linear (night) / 6s (day), one arc, no glow |
| Digit change | 180ms `cubic-bezier(0.16, 1, 0.3, 1)` roll, tabular so width never jumps |
| Sheet / navigation | 220ms ease-out |
| Route draw | 600ms stroke-dash draw on first appearance only |
| Link state | 200ms crossfade on the status dot |

`Reduce Motion`: sweep stops, digits crossfade in 120ms, route appears without draw. No animation may gate visibility of content.

## Platform conformance (HIG)

Applied, not aspirational. Each row is a check performed on the built screen.

| HIG rule | Implementation |
|---|---|
| Minimum text 11pt (iOS) | `micro` = `.caption2` (11pt), `label` = `.caption`; nothing smaller anywhere |
| Dynamic Type | every role uses a text style or `relativeTo:`; no `fixedSize:` |
| No app-specific appearance switch | theme follows the device, falls back to system appearance |
| Adapt to light/dark | Liberty Night + Liberty Day are the two appearances; both audited for contrast |
| Increase Contrast | `line` → `ink @ 55%`, `inkMuted` → `ink` when `colorSchemeContrast == .increased` |
| Never colour alone | every state also carries a glyph or word (`NO LINK`, `ABORTED`, `—`) |
| Hit target ≥ 44pt | `MotoMetrics.minTapTarget`; status band 54pt, buttons 52pt |
| Avoid light font weights | display 600/800, mono 500, prose 400 minimum |
| Minimise typefaces | one custom family (display numerals) + SF + SF Mono; everything readable is system |
| Respect safe areas | content inside `safeAreaInset`; map bleeds under the band, never under the home indicator |
| Reduce Motion | sweep stops, digits crossfade 120ms, route skips the draw |
| VoiceOver | radar and G-G plot expose one composed label with the numeric value, not the layer stack |
| Haptics are additive | `sensoryFeedback` on run start / finish / abort; never the only signal |

### Deliberate deviations

| Deviation | Why |
|---|---|
| Custom display font | Instrument numerals are the product signature and match the condensed face on the handlebar. HIG permits custom fonts when sizing and accessibility are handled, which `relativeTo:` plus a Dynamic Type cap does. |
| Branded palette instead of system colours | The two themes *are* the product; system colours would erase the day/night contract with the device. |
| No shadows, no card radius, no Liquid Glass | Elevation by surface steps and 1px rules is a print-poster stance, and it avoids the ghost-card / glass-on-glass tells. |

## Design skills: who decides what

Four skills are in play. They overlap, so the split is explicit; when two disagree, the row order below decides.

| Domain | Owning skill | Losing skill |
|---|---|---|
| Art direction, palettes, anti-slop bans, token discipline | `impeccable` | the iOS skills |
| Structural variety, pre-emit critique, anti-pattern catalogue | `hallmark` | — |
| SwiftUI correctness: state ownership, motion-as-state, gestures, adaptive layout, E2E gates, Fidelity rubric | `design-swiftui-interfaces` | — |
| iOS aesthetics audit: SF design axes, haptics, symbol effects, `numericText` | `ios-design-agent-skill` | — |

Known conflicts and rulings:

| Conflict | Ruling |
|---|---|
| `ios-design-agent-skill`: "system fonts only" | One custom display face survives, for numerals only. Every readable string is SF. Accessibility handled via `relativeTo:` + cap. |
| `ios-design-agent-skill`: rounded 12px cards + leading accent bars | Rejected — banned by `impeccable` (side-stripe) and `hallmark` (card stacking). Panels stay square with 1px rules. |
| `design-swiftui-interfaces`: native materials, Liquid Glass | Rejected for the content layer; the poster stance wins. Kept for system surfaces (sheets, menus, alerts). |
| Profile: standard vs expressive | **expressive**, because the brief asked for bold personality. Still one dominant motion idea per screen. |

Baseline static audit (`audit_swiftui_ui.py platforms/ios`): 30 MEDIUM, 0 HIGH/CRITICAL. The six that touch UI — hardcoded font sizes in the old `ContentView`, fixed frames, index-derived `ForEach` identity — are removed by the screen rewrite rather than patched.

## Round instrument (the handlebar screen)

The device is not a smaller phone. It is one physical object seen from a
metre away, in sun or at night, with gloves on. The system is five rules; every
page is a different readout of the same instrument.

### 1. One disc, one bezel, one band

- Paper disc radius 207 px inside a 466 px canvas, black casing 197–212,
  magenta band 212–222, near-black bezel outside.
- The **magenta band is the gauge**. Progress is painted on it in `ink`:
  speed, route progress, and the lean blip all use the same rim. There is no
  second track ring and no arc inside the disc.
- The band colour is brand, not state. It does not change between day and
  night and is never used for warnings.
- **The bezel is the battery ring** (2026-10-26, the rider's call; DESIGN_V2
  §2.9): Apple's ring in Apple's green, r 224–232 outside the band with a 2 px
  black seam, on every page of both interfaces. It is the one exception to
  "no second track ring" and "the bezel never changes", and it is data, so it
  is allowed a state colour (red at 20% and below) that the band is not.

### 2. Numbers are posters

The hero face is Barlow Condensed ExtraBold at **176 px** (hero subset:
digits, `° . : + - /`, N/S/E/W). One number per screen at that size; the next
largest thing is 40 px. Secondary values live on plates (24 px), never at a
size that competes.

| Role | Size | Colour |
|---|---|---|
| Hero numeral | 176 | `ink`, halo = `outline` |
| Unit / title | 40 | `ink` or `on_accent` |
| Plate + label | 24 | `on_accent` / `on_slab` |
| Micro (nav status, kicker) | 18 | `map_ink` / `on_slab` |

No page has a title label. The unit, the cardinal or the stopwatch is the
identity; "SPEED" over a speedometer is a word doing no work.

### 3. Two surfaces

| Surface | Day | Night |
|---|---|---|
| Paper (lean, speed, compass, launch, music) | cream `#F6F0D7` | midnight `#141024` |
| Map (navigation only) | green `#1E7A46` | midnight `#141024` |
| Ink (hero, horizon) | `#17151B` | `#F6F0D7` |
| Halo behind display type | paper (inverse of ink) | bezel |
| Band / plates | `#F34DAA` | `#F34DAA` |
| Counter slab | `#17151B` on paper | `#07050D` |

Day is a poster (ink on paper, magenta plates); night is the same poster
inverted. The green map is the only surface that is not the theme paper, and it
appears only where the map is the content. The bezel never changes.

### 4. Composition per page

| Page | One idea |
|---|---|
| Navigation | The map is the screen. Distance is the only large number; the maneuver glyph and the limit sign are the only props. |
| Lean | **A race lean gauge.** A segmented arc across the top, twelve 5° segments a side to 60°, fills to the lean in quarter degrees: HUD orange, HUD red past 45°, and the segment holding each side's peak keeps its unlit part in neon pink. The unsigned `42°` is the poster in the middle, `L`/`R` peaks beside it, Liberty City at dusk underneath. No horizon and no bike figure: the arc is the scale and the number is the reading. |
| Speed | The band is the speedometer: cream fills clockwise from twelve, magenta is what is left. |
| Compass | A rose on the band, a giant cardinal in the middle, the bearing in magenta beneath. |
| Launch (0–100) | A stopwatch, a shift strip, and the best time. Nothing else. |
| Music | A car-radio faceplate: source plate, track as station ident, three physical keys on one ink deck. |

State plates (`ARMED`, `LIVE`, maxima) are magenta; numeric counters (`BEST`,
`KM/H`, bearing) are ink slabs. Both are square: radius 0.

### 5. Type for phone data

Track titles and artist names are phone data, not UI strings. The instrument
faces are Latin-only, so the music page uses RAM copies of the descriptors
with **Noto Sans SC Bold** fallbacks generated from GB2312 level 1
(3,755 characters) at 40 px and 24 px. Chinese renders as CJK, Latin renders as
Barlow. The fallback descriptors are copied out of flash because the generated
tables are `const` and cannot be patched in place.

Cost: ~2.5 MB of flash for both CJK faces; the app partition has 8 MB and
the firmware still fits with more than half free.

### Review artefacts

`MOTO_UI_DUMP_DIR=/tmp/frames` on the native render suite writes one PPM per
theme and page plus the three link states. That is the visual review loop:
render, convert, look, fix. Nothing ships on a screenshot that was not also
run through the pixel tests.

## Poster sheet (phone surfaces)

Every sheet that is not the map uses the same four moves. They are what make a
phone screen read as the instrument rather than as a styled form.

1. **Band header, full bleed.** A `#F34DAA` block at the top, carrying a mono
   micro kicker (`DEVICE 01`, `POLICY 04`), the title at `.largeTitle` heavy in
   `onBand`, and the dismiss as ink type on the band. No navigation bar, no
   large-title collapse, no second plate in the header.
2. **One hero word per sheet.** `OFFLINE` / `ONLINE`, in the display face at
   76pt, with a mono detail line under it. Nothing else on the sheet is above
   `headline` size, which is the instrument's rule applied to a phone.
3. **Numbered sections.** `01 连接`, `02 圆屏显示`, with the index in the display
   face at 34 in `band`, the label at `headline`, and a 1px rule under the pair.
   The number is order, not decoration — the same grammar as the pause menu.
4. **Square panels, hairlines, no cards.** Rows are `MotoRow` inside one
   `MotoPanel` per group; a switch keeps its platform shape and takes the band
   as its tint.

Prose is set in the reading face (PingFang) at `.body` with 3pt line spacing —
never in mono, never in a tracked caps label.

### Race board (MENU → 08 赛道看板)

A logbook, not a live display: the handlebar is the live instrument. It
accumulates what the round screen measured while the phone listened, per day,
and shows today / 7 days / all (`MotoSegment`):

| Section | Poster | Chart |
|---|---|---|
| 0-100 | the best time at `display-xl` (the sheet's one hero), `BEST` in `accent` | every run in order as an ink line, the best point in `accent`, the average as a dashed rule |
| Lean | the deepest lean at `display-lg`, left and right on slabs | time per 5° bin, left mirrored against right, `route` (HUD orange), `abort` from 45°; the ±5° upright share as a line of text, because upright time would flatten every corner |
| G | the largest combined G at `display-lg` | the friction circle's envelope over the scope (casing, then `accent`), and accelerating / braking / left / right maxima on slabs |

Counters are ink slabs; the one state colour is `accent`. Every number is a
recorded one (`RaceLog` in `MotoNavigationCore`, `RaceLog.json` on the phone):
an empty scope says so instead of drawing zeros, the 0-100 times are the
round screen's (PerformanceRun, 0x19) and never re-timed on the phone, and
the time line says telemetry, not riding, because a connected screen parked on
a bench counts too. Clearing the log asks first.

## Do / Don't

- Do keep the frame loud and the data quiet. Don't texture a numeral.
- Do let one number own the screen. Don't place three equally weighted stats in a row.
- Do use `accent` for exactly one filled surface per screen (the command bar).
- Do make one word enormous. A 76pt wordmark next to 11pt mono is the whole trick.
- Do number the menu. It is how a pause screen reads.
- Do use the mono caps label at 11–12pt with `+0.16em`. Don't use an all-caps tracked eyebrow above every section.
- Do put the map edge-to-edge under the status band. Don't wrap the map in a rounded card.
- Don't use system blue, stock `List`/`Form` styling, glass blur, gradient text, or 16px+ card radii.
- Don't soften a rule to 0.5px or add a shadow to fake depth.
