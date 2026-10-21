# Design foundations

## Contents

- Source hierarchy
- Apple-native reasoning
- Eight decision lenses
- What “90% Apple” means
- Native-feeling checklist

## Source hierarchy

Use evidence in this order:

1. the project’s deployment target and installed SDK;
2. current Apple API documentation and Human Interface Guidelines;
3. current Apple sample projects and WWDC sessions;
4. established behavior of system apps and system components;
5. well-maintained community skills and articles;
6. personal taste.

When sources disagree, prefer the higher source and the newer SDK. Treat beta APIs as unstable and mark them. Do not copy Apple documentation or unlicensed community skill text into output; distill decisions and retain source links.

## Apple-native reasoning

An Apple-native interface is not a collection of visual motifs. It behaves predictably, gives people agency, preserves spatial context, adapts to the device, and uses motion to explain change. Visual polish supports those behaviors.

Before changing appearance, answer:

- What is the primary content?
- What is the primary action?
- What is permanent, contextual, transient, or destructive?
- Where did the presented element come from?
- How can the person reverse or cancel the action?
- What changes on a smaller window, larger text size, pointer, keyboard, or assistive technology?

## Eight decision lenses

Use Apple’s current design principles as decision lenses:

1. **Purpose:** keep the primary task obvious and remove effects that do not help it.
2. **Agency:** preserve control, undo, cancellation, and predictable navigation.
3. **Responsibility:** protect privacy, safety, attention, battery, and accessibility.
4. **Familiarity:** reuse platform conventions and consistent spatial relationships.
5. **Flexibility:** adapt to device, window, input, locale, appearance, and ability.
6. **Simplicity:** reduce conceptual and operational burden, not merely visual density.
7. **Craft:** tune typography, alignment, timing, state transitions, and edge cases.
8. **Delight:** create pleasure as the result of the first seven, not as decoration.

## What “90% Apple” means

Use the score in `validation.md` as a repeatable quality proxy. It means the result:

- passes every hard gate;
- scores at least 90/100 across platform fidelity, structure, semantics, continuity, motion, accessibility, and performance;
- has been exercised with realistic content and interrupted interactions;
- clearly reports anything not verified.

It does not imply Apple endorsement, App Review approval, or mathematical similarity to Apple’s private design system.

## Native-feeling checklist

- Put content before chrome.
- Prefer semantic hierarchy over decorative cards.
- Use standard navigation structures and toolbar placements.
- Keep controls near what they affect.
- Use direct labels and platform terminology.
- Preserve entrance/exit symmetry and source-destination continuity.
- Let content and windows resize; avoid fixed device assumptions.
- Use system typography and colors unless a brand need justifies an override.
- Use materials to communicate hierarchy, not to decorate every surface.
- Make feedback immediate, meaningful, and synchronized across visual and haptic channels.
- Design loading, empty, error, offline, permission-denied, and destructive states.
- Test the composition with no animation and no transparency; it should remain understandable.
