# Validation and Apple Fidelity rubric

## Contents

- Hard gates
- Runtime abuse tests
- Visual matrix
- Performance checks
- 100-point rubric
- Completion evidence

## Hard gates

Every applicable gate must pass:

1. the relevant scheme builds for the intended SDK;
2. the primary task completes without warnings, traps, or blocked controls;
3. rapid taps and mid-transition reversals do not create impossible state;
4. collection and transition identity remains stable;
5. smallest supported layout and largest text do not hide task-critical content;
6. light/dark and increased-contrast appearances remain legible;
7. Reduce Motion and Reduce Transparency have meaningful alternatives;
8. VoiceOver/keyboard/focus can reach and operate primary controls;
9. expressive effects have an older-OS/static fallback;
10. significant custom animation shows no unresolved hitch or hang in the sampled path.

No numeric score can override a failed hard gate.

## Runtime abuse tests

Exercise:

- double and repeated taps;
- tap during navigation;
- reverse a drag before and after the threshold;
- release a drag slowly and with high velocity;
- start a second gesture while settling;
- cancel presentation interactively;
- navigate back and reopen immediately;
- rotate or resize during loading and animation;
- background and foreground during an async transition;
- data refresh while a list item is selected;
- error/permission denial while a presentation is visible.

## Visual matrix

Capture previews or screenshots for applicable combinations:

| Axis | Minimum set |
| --- | --- |
| Container | smallest supported, typical, largest/resizable |
| Appearance | light, dark, increased contrast |
| Type | default, largest accessibility |
| Content | typical, long, empty, loading, error |
| Direction | left-to-right, right-to-left |
| Effects | normal, Reduce Motion, Reduce Transparency |
| Input | touch, pointer/keyboard/focus as applicable |

Review alignment, hierarchy, clipping, safe areas, contrast, hit targets, transition origins, material boundaries, and the settled frame after every hero motion.

## Performance checks

For custom motion, glass clusters, timelines, scroll effects, or shaders:

- profile the primary path with SwiftUI, Hangs, Animation Hitches, and Time Profiler instruments where available;
- inspect long or frequent view updates;
- distinguish main-thread blocking from rendering cost;
- verify offscreen effects stop;
- test on a lower-performance supported device;
- ensure the first interaction does not pay avoidable shader/image preparation cost.

Do not state “60/120 fps” unless measured on named hardware and a named path.

## 100-point rubric

Score conservatively:

| Category | Points | Full-credit evidence |
| --- | ---: | --- |
| Platform fidelity | 15 | Correct navigation, components, input, window/device behavior |
| Information architecture and layout | 15 | Clear hierarchy, adaptive composition, realistic edge states |
| Native semantics and visual system | 15 | Semantic controls/type/color/material, restrained customization |
| Interaction correctness and continuity | 20 | Single state ownership, stable identity, reversible/cancellable behavior |
| Motion and material craft | 15 | Causal, anchored, interruptible motion; coherent material hierarchy |
| Accessibility and adaptation | 10 | Dynamic Type, VoiceOver/focus, reduced effects, contrast |
| Performance and implementation quality | 10 | Clean build, bounded effects, no sampled hitches, maintainable code |

Interpretation:

- `90–100`: target reached, assuming all hard gates passed;
- `80–89`: polished but not yet stable or coherent enough;
- `70–79`: competent implementation with visible design debt;
- below `70`: redesign the structure or interaction model before adding polish.

Never award full points based only on code inspection when runtime or visual evidence is required.

Apply evidence caps:

- design proposal without an implementation: do not score, or label a nonquality planning estimate;
- static code review without a successful build: maximum `74`;
- successful build without runtime interaction and visual-matrix evidence: maximum `79`;
- no assistive-technology and reduced-effects verification: maximum `84`;
- expressive custom graphics without device profiling of the hero path: maximum `89`.

Only evidence that has actually been collected can lift a result above these caps. Label an incomplete score as provisional.

## Completion evidence

Report:

- exact scheme/device or preview configurations tested;
- accessibility settings tested;
- runtime abuse cases tested;
- profile selected;
- static audit result;
- Instruments evidence or why it was unavailable;
- rubric breakdown;
- remaining unverified risks.
