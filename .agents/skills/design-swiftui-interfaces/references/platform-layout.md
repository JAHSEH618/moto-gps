# Platform and adaptive layout

## Contents

- Shared rules
- iOS
- iPadOS
- macOS
- watchOS
- tvOS
- visionOS
- Content stress cases

## Shared rules

- Design from available container space, not a hardcoded screen rectangle.
- Respect safe areas, system bars, keyboards, window controls, and input affordances.
- Let semantic type styles and Dynamic Type drive intrinsic size.
- Use adaptive stacks, grids, split views, layout protocols, and size-class/window reasoning before fixed dimensions.
- Keep controls reachable and appropriately sized for the active input method.
- Preserve task hierarchy when a layout changes; do not merely reflow every item.
- Use asset catalog variants and semantic colors for appearance changes.

## iOS

- Optimize for one- and two-handed touch, short sessions, and frequent interruption.
- Keep primary actions reachable and expected back navigation intact.
- Use bottom/tab navigation only for peer destinations, not sequential steps.
- Avoid placing critical controls behind a gesture-only discovery path.
- Support portrait first unless the product requires otherwise; verify landscape and keyboard presentation.

## iPadOS

- Treat resizable windows and multitasking as primary contexts.
- Prefer split navigation, sidebars, inspectors, toolbars, and keyboard commands for deep workflows.
- Test narrow, medium, and full-width windows. Do not switch to a “compact phone UI” until the full composition actually stops fitting.
- Keep pointer hover and keyboard focus additive to touch behavior.
- Make drag and drop, selection, and context menus consistent with platform expectations.

## macOS

- Design for precise pointer input, menus, keyboard shortcuts, multiple windows, resizable content, and denser information.
- Use standard toolbar, sidebar, inspector, table, settings, and menu patterns.
- Preserve window minimum size without clipping or trapping content.
- Do not use oversized iOS touch spacing everywhere.
- Support focus rings, tab navigation, hover, right-click/context menus, and command discoverability.

## watchOS

- Keep tasks glanceable, brief, and immediately actionable.
- Use system navigation, typography, Digital Crown behavior, and complications/widgets appropriately.
- Reduce simultaneous choices and decorative motion.
- Test always-on and reduced-luminance behavior where relevant.

## tvOS

- Make focus movement, selection state, and remote input obvious.
- Keep important content within comfortable viewing and focus regions.
- Use focus-driven scale/depth carefully; do not compete with the system focus engine.

## visionOS

- Preserve comfortable depth, scale, and viewing distance.
- Use windows, volumes, ornaments, hover, gaze, pinch, and spatial audio intentionally.
- Avoid rapid z-axis movement, full-field motion, and dense controls.
- Provide a windowed or less immersive alternative when possible.

## Content stress cases

Validate:

- smallest and largest supported container;
- portrait and landscape where supported;
- split view and Stage Manager/window resizing;
- text at the largest accessibility sizes;
- long localized strings and right-to-left layout;
- empty, loading, error, offline, and permission-denied content;
- hardware and software keyboard;
- pointer/trackpad and touch;
- light, dark, increased contrast, and reduced transparency;
- content arriving late or changing during a transition.
