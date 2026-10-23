# Design

Visual system for the MOTO GPS phone app (`platforms/ios`) and the web shell (`platforms/web/shell`). One structure, two themes. Themes are triggered by ambient light, not by taste.

## Theme switch

| Theme | Trigger | Lighting condition |
|---|---|---|
| **Vice** | device theme `Night`, or system dark mode when the device is unreachable | Dark garage, night ride. Phone must not blind the rider. |
| **Heat** | device theme `Day`, or system light mode when the device is unreachable | Daylight, glare on the screen. Readability beats atmosphere. |

`DeviceSettings.theme = Auto` maps through the device; the app never contradicts the handlebar. There is **no app-level appearance switch** — HIG § Dark Mode: an app-specific appearance setting makes people adjust two places and suspect a broken app. The device owns the choice; losing the link falls back to the system appearance.

`Auto` needs a light condition the handlebar does not have: there is no clock, light sensor or network time on the ESP32-S3, so the phone supplies one day/night flag with the settings and the device resolves `Auto` to `Day` or `Night` from it (day until the first flag arrives). An explicit `Day` or `Night` ignores the flag. The control therefore reads **Auto / Day / Night** on the device-settings screen; Vice and Heat are the phone palettes that follow the result, not a second setting.

Both themes share geometry, spacing, type scale, and the circle motif. Only paper band, accent, display weight, and texture change — never structure. If a component needs a different layout in the other theme, the component is wrong.

**Heat is a light theme on purpose.** A dark interface behind glass in direct sun becomes a mirror: reflectance wins and the numerals disappear. Readability in daylight requires a high-luminance paper and near-black ink, which is also the palette of the poster the theme is named after. Atmosphere is sacrificed for legibility; that is the trade.

## Color

All values are exact; no gradients on data surfaces.

### Vice (night instrument console)

| Token | Value | Use |
|---|---|---|
| `bg` | `#060808` | app background |
| `surface` | `#0E1211` | panels, sheets |
| `surface-2` | `#141918` | raised row, selected row background |
| `line` | `#232B2A` | 1px rules |
| `line-soft` | `rgba(255,255,255,0.10)` | grid, dividers inside panels |
| `ink` | `#F2F4EE` | primary text, numerals |
| `ink-muted` | `#93A09C` | secondary text, labels |
| `accent` | `#FFB72E` | happening now: active route, primary button, run timer |
| `link` | `#5CC7FF` | BLE link, GPS fix |
| `ok` | `#73E6A4` | completed, saved |
| `abort` | `#FF655F` | aborted run, off-route |

### Heat (daylight poster)

| Token | Value | Use |
|---|---|---|
| `bg` | `#F2E6CE` | sunlit paper |
| `surface` | `#EBDBBC` | panels |
| `surface-2` | `#E3D0AB` | raised row, pressed state |
| `line` | `ink @ 24%` | 1px rules — heavier than Vice so they survive glare |
| `line-soft` | `ink @ 12%` | dividers, halftone grid |
| `ink` | `#17110D` | primary text, numerals |
| `ink-muted` | `#5C4A38` | secondary text only, never below 15pt |
| `accent` | `#E8500F` | fills and large numerals; **never body-size text** |
| `link` | `#0A5C78` | BLE link, GPS fix |
| `ok` | `#1D6B3C` | completed |
| `abort` | `#B3241C` | aborted |

**Heat accent rule.** `#E8500F` on paper is 3.9:1 — enough for 34pt+ numerals and for fills carrying `ink` text (5.0:1), not enough for prose or 11pt labels. Small accent-coloured text is a defect in this theme.

### Contrast (measured, sRGB relative luminance)

| Pair | Ratio | Level |
|---|---|---|
| Vice `ink` on `bg` | 17.4:1 | AAA |
| Vice `ink-muted` on `bg` | 7.5:1 | AAA (large) / AA (body) |
| Vice `accent` on `bg` | 11.6:1 | AAA |
| Vice `link` on `bg` | 10.7:1 | AAA |
| Vice `abort` on `bg` | 7.0:1 | AA |
| Heat `ink` on `bg` | 15.1:1 | AAA |
| Heat `ink-muted` on `bg` | 6.8:1 | AA |
| Heat `accent` on `bg` | 3.9:1 | AA-large only |
| Heat `ink` on `accent` | 5.0:1 | AA |
| Heat `link` on `bg` | 6.0:1 | AA |
| Heat `abort` on `bg` | 5.3:1 | AA |

`ink-muted` is never used for data in either theme; it is for labels and secondary prose.

## Typography

Two families plus the system mono. No italic anywhere in headings, labels, or data.

| Role | Family | Spec |
|---|---|---|
| Display numerals | Barlow Condensed (bundled, OFL) | 600 Vice / 800 Heat, tracking `-0.01em`, tabular |
| Labels, units, mono meta | SF Mono (iOS) / `ui-monospace` (web) | 500–600, caps, tracking `+0.16em` |
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
- **Grid**: 44pt squares at 1.8% white on Night, halftone dots at 6pt on Dusk, clipped to panels — never under data.

## Circle motif

The handlebar screen is round; the phone repeats it at three sizes.

| Size | Where | Content |
|---|---|---|
| 248 | Home hero | poster street abstraction + sweep, `N` badge on the rim |
| 176 | Active ride | the real route window, streets and footprints around the rider |
| 44 | Status band | connection state ring |

Radar chrome (GTA V reference, `docs/todo.md` 22:58): **2px `ink` outer ring**, rider as a chunky outlined arrow, route stroke in `route` magenta with a 1pt `bg` casing. Devices that ride on the ring and never float over the map:

| Device | Spec |
|---|---|
| Safehouse blip | `ink` house glyph with a `bg` outline, inside the ring at ~0.88 radius, upper-left quadrant. Present only when home is set. |
| Compass badge | Circular `ink` badge, `bg` letter `N`, centred on the rim at twelve o'clock. |
| Progress ring | **The ring is the gauge.** The completed fraction of `route_progress_m / total_distance_m` replaces that segment of the 2px rim in `accent`, starting at twelve o'clock. No second track ring: a separate arc read as a halo. Real data only; never repurposed as a health bar. |
| Counter bar | `ink` slab with `onSlab` numerals in a corner, the way GTA parks cash. |

Map fill inside the circle: `surface` base, park at `ok`, water at `link`, roads and blocks in `ink`. Strength is per theme because dark-on-cream at 10% disappears while light-on-black at 10% reads:

| Layer | Vice | Heat |
|---|---|---|
| Blocks / footprints | `ink` 10% | `ink` 17% (abstraction) · 10% (real footprints) |
| Roads | `ink` 24% (abstraction) · 55% (real) | `ink` 62% (abstraction) · 34% (real) |
| Park / water | 24% / 22% | 34% / 30% |

The home radar draws a poster abstraction — a city that is clearly not a claim about a location. The ride radar draws the same real window and route the handlebar gets: a live route over invented streets is a lie, so real geometry replaces the grid as soon as a scene exists.

Texture inside the circle only: Vice draws 1pt scanlines at 4pt pitch (`line-soft`). **Heat draws none** — thin high-frequency bands turn into haze in glare and the map stops reading as a map. Texture never overlays the rider dot or route.

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
| Radar sweep | 4s linear (Vice) / 6s (Heat), one arc, no glow |
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
| Adapt to light/dark | Vice + Heat are the two appearances; both audited for contrast |
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

- Paper disc radius 207 px inside a 466 px canvas, black casing 197–207,
  magenta band 212–222, near-black bezel outside.
- The **magenta band is the gauge**. Progress is painted on it in `ink`:
  speed, route progress, and the lean blip all use the same rim. There is no
  second track ring and no arc inside the disc.
- The band colour is brand, not state. It does not change between day and
  night and is never used for warnings.

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
| Lean | **An artificial horizon.** The disc is the bike; the world tilts and the sun rides the horizon. The rim blip marks the angle; `+42°` is printed on the world. |
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
