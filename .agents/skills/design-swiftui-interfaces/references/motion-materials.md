# Motion, materials, and expressive graphics

## Contents

- Motion grammar
- Connected transitions
- Liquid Glass
- Blur and depth
- Advanced graphics
- Haptics and sound
- Reduced-effects choreography
- Performance rules

## Motion grammar

Use motion to explain cause, hierarchy, direction, and completion.

- Respond immediately to input.
- Track direct manipulation continuously.
- Use springs for retargetable and gesture-driven changes.
- Preserve current velocity when replacing one spring with another.
- Anchor presentation to the control or content that caused it.
- Return along a compatible path when reversing.
- Keep related properties on a shared progress value or transaction.
- Prefer a small motion vocabulary reused throughout the product.

For a gesture-driven surface, define direct tracking, resistance beyond bounds, projected destination, release velocity, settling spring, interrupt behavior, and cancel behavior.

## Connected transitions

Prefer transitions that preserve object identity:

- use system navigation/presentation transitions when correct;
- use source-linked zoom transitions for an item becoming its detail;
- use matched geometry for a visual element that remains conceptually the same;
- use content transitions for value/symbol/text changes;
- use phase/keyframe animation for a bounded multi-stage moment;
- keep the source and destination shapes, clipping, and z-order compatible.

Avoid cross-fading everything. Cross-fade when spatial movement would be misleading, Reduce Motion is active, or there is no meaningful source-destination relationship.

## Liquid Glass

Treat Liquid Glass as a dynamic functional layer for controls and navigation.

- Prefer system controls, bars, sheets, menus, and presentations so the system supplies current behavior.
- Apply custom glass after layout and appearance modifiers.
- Group related glass elements in `GlassEffectContainer`.
- Use stable IDs and the framework’s glass transition APIs for morphing.
- Use interactivity only for elements that actually respond to touch or pointer input.
- Tint selectively to communicate emphasis or identity.
- Let content scroll beneath functional chrome where the platform pattern supports it.
- Avoid placing glass throughout the content layer.
- Avoid stacking separate glass effects; glass cannot sample other glass correctly.
- Provide an opaque/material fallback for Reduce Transparency and older OS versions.

## Blur and depth

Distinguish several effects instead of applying a generic blur:

1. **Material separation:** use system materials to keep foreground controls legible over content.
2. **Focus transition:** combine restrained blur, scale, dimming, and depth to move attention between layers.
3. **Scroll-edge separation:** let the system adapt the boundary between scrolling content and chrome.
4. **Atmospheric color:** soften artwork or a mesh/color field behind readable content.
5. **Motion blur or distortion:** use a bounded shader only when it explains speed, transformation, or energy.

Do not animate into or out of blur when Reduce Motion is active. Do not blur text that must remain readable. Do not use blur to hide poor hierarchy.

## Advanced graphics

Use SwiftUI’s native composition pipeline before reaching for a custom renderer:

- `visualEffect` and `scrollTransition` for geometry/scroll-aware transforms;
- `MeshGradient` for rich color fields;
- `Canvas` and `TimelineView` for controlled drawing and time-driven visuals;
- symbol effects for stateful SF Symbol feedback;
- color, distortion, and layer shader effects for effects that truly need per-pixel work;
- alignment guides, masks, overlays, and blend modes for compositional effects.

Verify every API and availability against the installed SDK. Gate new APIs with `#available` and provide a meaningful fallback. Compile Metal shaders during preparation rather than at the first visible interaction when applicable.

For the expressive profile, decompose a hero effect into a pipeline:

1. source data or artwork;
2. static base composition;
3. bounded color/blur treatment;
4. time or gesture driver;
5. spatial transition;
6. readable foreground;
7. reduced/static fallback.

## Haptics and sound

- Trigger feedback at the causal frame: snap, commit, success, warning, or error.
- Match intensity and character to the visual event.
- Use SwiftUI sensory feedback APIs when they fit and verify platform availability.
- Do not fire repeated haptics during every animation frame or scroll update.
- Never make haptics or sound the only indication of a state change.

## Reduced-effects choreography

When Reduce Motion is active:

- remove large translations, zooms, parallax, depth-axis motion, and repeated ambient motion;
- tighten springs and remove bounce;
- replace spatial transitions with short fades or immediate state changes;
- keep gesture tracking direct when tracking helps control;
- avoid animating blur.

When Reduce Transparency is active:

- replace glass/translucent backgrounds with opaque semantic surfaces;
- preserve hierarchy with borders, grouping, color, and spacing;
- disable atmosphere that depends on seeing through layers.

When Increase Contrast or Differentiate Without Color is active:

- strengthen boundaries and labels;
- add shapes, symbols, or text so color is never the only state cue.

## Performance rules

- Use one glass container for a related cluster, not many overlapping containers.
- Limit the number and area of live glass/shader surfaces.
- Reduce effect resolution or complexity before dropping frame continuity.
- Pause offscreen and background animation.
- Avoid state writes on every `TimelineView` or scroll update unless required.
- Test the oldest supported device class for expressive work.
- Use Instruments to distinguish main-thread blocking, excessive SwiftUI updates, and GPU-heavy effects.
