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

All values are exact; no gradients on data surfaces. The phone palette is the
handlebar instrument's palette, token for token: cream paper and outline black
by day, midnight purple and sunset cream by night, hot pink for the band and for
whatever is happening now. A rider looking from the bar to the phone is looking
at one product.

### Vice (night paper)

| Token | Value | Use |
|---|---|---|
| `bg` | `#141024` | app background — the instrument's midnight paper |
| `surface` | `#191430` | panels, sheets |
| `surface-2` | `#211A3C` | raised row, selected row background |
| `line` | `#2A2145` | 1px rules |
| `line-soft` | `rgba(255,255,255,0.08)` | grid, dividers inside panels |
| `ink` | `#F6F0D7` | primary text, numerals — sunset cream |
| `ink-muted` | `#B9B1C9` | secondary text, labels |
| `accent` | `#F34DAA` | the band: active route, primary button, run timer |
| `route` | `#FF5ACD` | route stroke (the night variant of the band) |
| `link` | `#42F5D7` | BLE link, GPS fix |
| `ok` | `#73E6A4` | completed, saved |
| `abort` | `#FF655F` | aborted run, off-route |

### Heat (day paper)

| Token | Value | Use |
|---|---|---|
| `bg` | `#F6F0D7` | road-cream paper |
| `surface` | `#EFE7C9` | panels |
| `surface-2` | `#E6DCB8` | raised row, pressed state |
| `line` | `ink @ 24%` | 1px rules — heavier than Vice so they survive glare |
| `line-soft` | `ink @ 12%` | dividers, halftone grid |
| `ink` | `#17151B` | outline black: primary text, numerals |
| `ink-muted` | `#5C4A38` | secondary text only, never below 15pt |
| `accent` | `#F34DAA` | the band: fills and large numerals only |
| `route` | `#F34DAA` | route stroke — same magenta as the band by day |
| `link` | `#0E6F68` | BLE link, GPS fix |
| `ok` | `#1D6B3C` | completed |
| `abort` | `#B3241C` | aborted |

**The band is brand, not state.** `#F34DAA` is identical in both themes, it never
signals a warning, and it is used for exactly one filled surface per screen.
`onAccent` is ink in both themes — cream on magenta measures 2.8:1 and fails.

**Heat accent rule.** `#F34DAA` on paper is 2.8:1 — enough for a fill carrying
`ink` text (5.6:1), not enough for prose, numerals below 34pt, or any 11pt label.
Small accent-coloured text is a defect in this theme. On Vice the same magenta
clears 5.7:1, so the rule is theme-specific even though the value is not.

### Contrast (measured, sRGB relative luminance)

| Pair | Ratio | Level |
|---|---|---|
| Vice `ink` on `bg` | 16.3:1 | AAA |
| Vice `ink-muted` on `bg` | 9.0:1 | AAA |
| Vice `accent` on `bg` | 5.7:1 | AA |
| Vice `link` on `bg` | 13.5:1 | AAA |
| Vice `abort` on `bg` | 6.4:1 | AA |
| Heat `ink` on `bg` | 15.8:1 | AAA |
| Heat `ink-muted` on `bg` | 7.4:1 | AAA |
| Heat `accent` on `bg` | 2.8:1 | fails text; fills only |
| Heat `ink` on `accent` | 5.6:1 | AA |
| Heat `link` on `bg` | 5.3:1 | AA |
| Heat `abort` on `bg` | 5.8:1 | AA |
| Both: `on-slab` on `slab` | 17.5:1 | AAA |

`ink-muted` is never used for data in either theme; it is for labels and secondary
prose, and never on the slab — the slab has its own `on-slab` token for that.

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
