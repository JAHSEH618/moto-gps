# Product

## Register

product

## Users

Riders of a custom e-scooter/e-bike who have a Waveshare 1.75" round ESP32 screen mounted on the handlebar. The phone is mounted or in a jacket pocket; the screen is the primary instrument. Two contexts, both short:

- **Before the ride**, standing next to the bike: pick a destination, check the link to the screen, start.
- **During the ride**, one handed, gloves, low light or direct sun: glance at distance to the next turn, speed, Lean.

The rider is not browsing. Every screen answers one question in under a second.

## Product Purpose

Phone-side companion for the round handlebar display: destination search, route planning, BLE bridge to the device, attitude/performance telemetry, and ride records. Success is a rider trusting the pair enough to stop looking at the phone mid-ride — the phone sets things up, the handlebar shows the ride.

## Brand Personality

Technical, cinematic, unfussy. An instrument panel that happens to be beautiful. It borrows the visual grammar of open-world driving games (radar, HUD band, condensed numerals, dusk silhouettes) without becoming a toy: numbers stay legible, states stay honest.

Three words: **instrument · dusk · exact**.

## Anti-references

- The stock iOS Settings/List look the app shipped with before this redesign: `List` + `Section` + system blue, no identity.
- Generic "AI dark mode": purple-to-blue gradients, glass cards, glowing borders, 32px rounded corners.
- SaaS metric cards: big number + small label + gradient accent.
- Cartoon racing games: chrome, flames, speedometer bezels, fake leather. This is not a game skin.
- Anything that makes the rider hunt for a value. Drama is allowed in the frame, never in the data.

## Design Principles

1. **The circle is the product.** The handlebar screen is round; the phone repeats that circle as its signature shape. Recognition before ornament.
2. **Two lighting conditions, two themes.** Vice (night) is an instrument console: dim, hard-edged, amber. Heat (daylight) is a poster: sunlit paper, near-black ink, hot orange. Daylight readability outranks atmosphere in Heat. Theme follows the device's day/night setting by default.
3. **Data is never decorated.** Numbers are the brightest, largest thing on the screen. Texture, banding and halftone live in the frame, behind and around the data, never on it.
4. **One accent per state.** Amber means "happening now". Ice means "link". Green means "done". Red means "aborted". No colour is decorative.
5. **No card stacking.** Panels are etched planes with 1px rules, not floating rounded boxes. The map is a surface, not a background image inside a card.

## Accessibility & Inclusion

- WCAG AA minimum, AAA for primary data (verified in DESIGN.md).
- Dynamic Type supported for all prose and labels; display numerals scale on a capped curve so the instrument never reflows mid-ride.
- `Reduce Motion` replaces the radar sweep and digit roll with a crossfade; no animation gates content visibility.
- Colour is never the only carrier of state: every state also has a text or glyph token.
- VoiceOver: the radar and G-G plot expose a single composed label with the numeric readout, not the raw layer stack.
