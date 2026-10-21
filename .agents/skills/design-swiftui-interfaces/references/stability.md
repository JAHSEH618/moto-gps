# Interaction stability

## Contents

- Model interactions explicitly
- Own state once
- Arbitrate gestures
- Preserve identity and continuity
- Avoid timing races
- Protect the render loop
- Reproduction checklist

## Model interactions explicitly

Represent meaningful UI modes with an enum or a small state machine instead of independent booleans that can form impossible combinations.

Examples of meaningful modes:

- collapsed, dragging, expanded, dismissing;
- idle, loading, loaded, empty, failed;
- browsing, selecting, editing, confirming.

For each event, define the valid source states, next state, side effects, cancellation, and visual transition. Ignore or queue invalid events deliberately; do not let them mutate several unrelated flags.

## Own state once

- Give each piece of persistent UI state one owner.
- Use gesture state for transient drag/press values and model state for settled results.
- Derive scale, opacity, blur, offset, and material from the same progress value when they describe one transition.
- Avoid mirroring navigation state in both path values and booleans.
- Cancel obsolete async work when the owning state changes or the view disappears.
- Keep animation completion from overwriting a newer user action.

## Arbitrate gestures

- Prefer semantic controls before custom gestures.
- Give drag gestures a clear activation region, axis, and threshold.
- Preserve the touch-to-content offset during direct manipulation.
- Resolve scroll-versus-drag intent before committing a nested gesture.
- Use simultaneous or high-priority gesture composition only with a documented reason.
- Ensure a full-screen overlay does not steal taps from visible controls.
- Test a gesture starting near navigation edges, inside a scroll view, on a control, and during an existing transition.
- On release, decide the destination from position and velocity; hand current velocity into the settling animation where the API supports it.
- Allow mid-flight reversal for interactive sheets, cards, and draggable controls.

## Preserve identity and continuity

- Use stable model IDs for `ForEach`, navigation destinations, matched geometry, and glass effects.
- Never create `UUID()` during view construction to force refresh.
- Keep the source view alive long enough for a source-linked transition.
- Use one namespace and stable IDs for a single transition family.
- Avoid changing layout identity and navigation state in unrelated transactions.
- Enter and exit through spatially consistent paths.

## Avoid timing races

Do not coordinate UI state with arbitrary `asyncAfter` or `Task.sleep` calls. They fail under rapid interaction, cancellation, backgrounding, changed animation duration, and Reduce Motion.

Prefer:

- a single state transition with derived visuals;
- animation completion criteria where available;
- phase or keyframe animation for a bounded sequence;
- an async task whose lifetime is tied to the state owner and is cancellable;
- transactions that group logically related changes.

If a delay is part of the product behavior, make it explicit, cancellable, and independent from assumptions about an animation’s duration.

## Protect the render loop

- Move image decoding, file I/O, network work, and expensive calculation out of view-body updates.
- Avoid high-frequency global environment changes.
- Keep geometry/scroll observers from writing state on every frame unless the output is quantized or meaningfully changed.
- Precompile or prepare expensive shader resources outside the first interaction.
- Bound shader and blur surfaces; do not apply full-screen offscreen effects by default.
- Pause timelines, ambient effects, and media-driven animation when offscreen or inactive.
- Profile before “optimizing” by removing meaningful motion.

## Reproduction checklist

For each reported interaction bug, capture:

1. starting screen and state;
2. exact taps, gestures, velocity/direction, and timing;
3. device/window size and input method;
4. expected state and actual state;
5. whether it reproduces with Reduce Motion;
6. whether rapid repetition or reversal is required;
7. logs, warnings, or animation-hitch evidence.

After a fix, repeat slowly, rapidly, during an in-flight transition, after backgrounding, with large text, and on the smallest supported layout.
