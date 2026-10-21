---
name: design-swiftui-interfaces
description: Design, implement, refactor, or review polished native SwiftUI interfaces for iOS, iPadOS, macOS, watchOS, tvOS, and visionOS. Use for Apple-like UI/UX, Human Interface Guidelines alignment, interaction bugs, gestures, navigation, adaptive layout, Dynamic Type, accessibility, animation, transitions, materials, Liquid Glass, blur, shaders, haptics, visual polish, or visual QA. Supports a restrained standard profile and a colorful expressive profile with cinematic connected motion while enforcing build, interaction, accessibility, performance, and reduced-effects gates.
---

# Design SwiftUI Interfaces

Build interfaces that feel native because their structure, behavior, motion, and materials form one coherent system. Treat “90% Apple-quality” as a review target backed by hard gates and a weighted rubric, never as a visual claim that can replace testing.

## Operating contract

- Make stability the first feature. Do not add expressive effects until the underlying task, state model, layout, gestures, and navigation behave correctly.
- Default to the **standard** profile. Use the **expressive** profile only when the user explicitly asks for bold, colorful, cinematic, highly animated, or personality-rich work.
- Keep both profiles recognizably Apple-native. Change intensity, not interaction semantics.
- Prefer system components and semantic styles. Customize after the native hierarchy works.
- Make every motion causal, continuous, interruptible where interactive, spatially anchored, and reversible where the action is reversible.
- Provide Reduce Motion, Reduce Transparency, Increase Contrast, Differentiate Without Color, Dynamic Type, keyboard/focus, and VoiceOver behavior as part of the implementation.
- Verify unfamiliar or release-specific APIs against the installed SDK or current Apple documentation. Never invent a SwiftUI modifier or parameter from memory.
- Preserve existing project conventions and unrelated user changes.

## Workflow

### 1. Inspect before designing

1. Read repository instructions and inspect the current UI, navigation model, deployment targets, assets, and design tokens.
2. Identify the supported platform, window/device classes, input methods, and minimum OS.
3. Check the local toolchain with `xcodebuild -version`, `xcrun swift --version`, and project build settings when available.
4. Reproduce reported interaction bugs before changing visuals. Record the exact trigger, observed state, and expected state.
5. Read:
   - `references/design-foundations.md`
   - `references/profiles.md`
   - `references/stability.md`
   - the task-relevant references from the router below.

If full Xcode, a simulator, or a required SDK is unavailable, continue with code and static review where possible, but state which runtime gates remain unverified.

### 2. Establish the experience contract

Write a compact internal brief before implementation:

- primary user task and emotional target;
- selected profile: `standard` or `expressive`;
- supported platforms, sizes, orientations, and input methods;
- source and destination for each navigation transition;
- finite UI states and the single owner of each state;
- motion purpose for each major transition;
- reduced-motion and reduced-transparency equivalents;
- performance-sensitive surfaces.

For a new or substantially redesigned experience, explore two or three structural variations before polishing. Use realistic content, long strings, empty/error/loading states, and interrupted gestures. Do not generate multiple decorative skins around the same weak structure.

### 3. Build the stable native skeleton

1. Implement information architecture and navigation with native containers.
2. Use semantic typography, colors, materials, spacing, safe areas, and control roles.
3. Make the layout adaptive before adding fixed visual tuning.
4. Use `Button`, `Toggle`, `Menu`, `NavigationLink`, `List`, `Form`, `Toolbar`, and platform components when their semantics match.
5. Establish stable identity for collections and transitions.
6. Keep one source of truth per interaction. Derive visual state from model and gesture state instead of mirroring it in multiple booleans.
7. Build and exercise the primary task before proceeding.

### 4. Add motion as a state transition

For every nontrivial animation, define:

1. trigger;
2. start state;
3. continuous path or intermediate state;
4. settled state;
5. interruption/reversal behavior;
6. cancellation behavior;
7. reduced-motion equivalent.

Use springs for interactive and retargetable motion. Preserve velocity across gesture handoff. Anchor transitions to the control or content that caused them. Avoid arbitrary delays as coordination; sequence from state, completion, phase/keyframe APIs, or explicit transaction boundaries.

Read `references/motion-materials.md` before adding custom motion, Liquid Glass, blur, mesh gradients, shaders, parallax, or haptics.

### 5. Apply the selected profile

- **Standard:** emphasize content, familiar structure, low-amplitude motion, system materials, restrained color, and quiet delight.
- **Expressive:** add selected hero moments with colorful glass, connected morphs, richer depth, scroll-reactive chrome, ambient color fields, shader-backed atmosphere, and synchronized haptics.

In expressive work, choose at most one dominant motion idea per screen and one hero transition per task flow. Maintain a quiet layer so the focal effect remains legible. Do not put Liquid Glass in the content layer merely to make the screen look futuristic.

### 6. Validate before calling the work complete

1. Run the static risk scan:

   ```bash
   python3 "$HOME/.codex/skills/design-swiftui-interfaces/scripts/audit_swiftui_ui.py" <project-path>
   ```

   Add `--profile expressive --fail-on high` for expressive implementations.

2. Build the relevant schemes and run existing tests.
3. Exercise rapid repeated taps, gesture cancellation, mid-animation reversal, navigation back-and-forth, task cancellation, and background/foreground transitions.
4. Review previews or screenshots using the matrix in `references/validation.md`.
5. Test accessibility alternatives and keyboard/focus behavior.
6. Profile significant custom motion or graphics with SwiftUI, Hangs, Animation Hitches, and Time Profiler instruments when Xcode supports them.
7. Score an implemented result with the Apple Fidelity rubric. Apply its evidence caps; a score below 90 requires another pass, and hard-gate failures block completion regardless of score.

## Hard rules

- Do not use delayed asynchronous work as the primary animation state machine.
- Do not attach `onTapGesture` to a visual element when a semantic control fits.
- Do not create overlapping invisible hit targets or gesture layers.
- Do not use transient IDs such as `UUID()` during view construction.
- Do not attach unscoped `.animation(...)`; bind animation to the value that changes or use an explicit transaction.
- Do not animate layout with chains of unrelated booleans.
- Do not block main-thread rendering with image decoding, I/O, model work, or shader setup.
- Do not encode meaning through color, blur, motion, or haptics alone.
- Do not stack glass on glass. Group related glass shapes in the framework-provided container.
- Do not use custom shaders for ordinary controls that the system already renders correctly.
- Do not force an iPhone composition onto iPad or macOS by scaling it up.
- Do not hide unverified limitations. Report what was built, tested, and not tested.

## Reference router

| Need | Read |
| --- | --- |
| Apple design reasoning and source hierarchy | `references/design-foundations.md` |
| Standard vs. expressive rules | `references/profiles.md` |
| Interaction bugs, gesture arbitration, state ownership | `references/stability.md` |
| Springs, transitions, glass, blur, shaders, haptics | `references/motion-materials.md` |
| iPhone, iPad, Mac, Watch, TV, Vision adaptation | `references/platform-layout.md` |
| SwiftUI API selection and availability discipline | `references/implementation.md` |
| Accessibility and reduced-effects behavior | `references/accessibility.md` |
| Test matrix, hard gates, and 100-point rubric | `references/validation.md` |
| Apple and community research links | `references/source-index.md` |

## Handoff format

State:

1. selected profile and why;
2. interaction model and major design decisions;
3. files changed;
4. build, runtime, visual, accessibility, and performance checks performed;
5. Apple Fidelity score with category breakdown;
6. unverified risks or deliberate tradeoffs.

Avoid describing an interface as “Apple-quality” solely because it uses blur, rounded rectangles, or spring animations.
