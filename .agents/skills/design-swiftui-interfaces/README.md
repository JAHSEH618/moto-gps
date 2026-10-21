# SwiftUI Interface Design Skill

> A stability-first Codex Agent Skill for building polished, accessible, native SwiftUI interfaces across Apple platforms.

[![Validate Skill](https://github.com/ZHUOLIN0928/swiftui-interface-design-skill/actions/workflows/ci.yml/badge.svg)](https://github.com/ZHUOLIN0928/swiftui-interface-design-skill/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

This open-source Skill turns Apple Human Interface Guidelines principles into a repeatable workflow for **iOS, iPadOS, macOS, watchOS, tvOS, and visionOS**. It makes interaction correctness, adaptive layout, accessibility, motion, materials, and visual polish part of one review loop.

> This project is independent and is not affiliated with Apple Inc.

## Why use it

SwiftUI interfaces often look convincing in a static preview while failing under rapid taps, interrupted gestures, Dynamic Type, keyboard focus, reduced effects, or window resizing. This Skill makes those failure modes explicit before adding decorative polish.

| Focus | What the Skill enforces |
| --- | --- |
| Stability | One owner for state, stable identity, cancellation, reversible gestures, and interruption-safe transitions |
| Native structure | Semantic controls, platform-appropriate navigation, adaptive containers, and system materials |
| Motion and materials | Causal, anchored, retargetable motion with Reduce Motion/Transparency fallbacks |
| Accessibility | Dynamic Type, VoiceOver, keyboard/focus, contrast, RTL, and differentiated non-color cues |
| Evidence | Build, runtime abuse, visual matrix, performance, and Apple Fidelity review gates |

## Quick start

1. Install this directory as a Codex Skill at:

   ```text
   <Codex skills directory>/design-swiftui-interfaces
   ```

2. Invoke it in Codex:

   ```text
   $design-swiftui-interfaces
   ```

3. Tell the agent the target platform, deployment range, primary task, supported sizes/input methods, and whether the `standard` or `expressive` profile is intended.

The Skill can review SwiftUI code on any machine. Building and runtime verification of Apple targets still requires the applicable Xcode toolchain, SDKs, previews, simulators, or devices.

## Profiles

### `standard` (default)

Use for settings, productivity, education, medical, finance, enterprise, and other task-focused interfaces. It favors familiar hierarchy, quiet surfaces, restrained motion, system materials, and high information clarity.

### `expressive` (opt in)

Use when a product explicitly needs colorful glass, connected morphs, richer depth, scroll-reactive chrome, ambient color, or cinematic transitions. It changes intensity—not semantics—and requires a quiet content layer, reduced-effects alternatives, and effect-specific performance evidence.

## Validate an implementation

Run the conservative static risk scan from this repository:

```bash
python3 scripts/audit_swiftui_ui.py path/to/swiftui-project --profile standard
python3 scripts/audit_swiftui_ui.py path/to/swiftui-project --profile expressive --fail-on high
```

The scanner is a review aid, not proof of a correct build or accessible interaction. Follow the [validation matrix](references/validation.md) and report the exact schemes, SDKs, devices/simulators, accessibility settings, gestures, and performance paths that were tested.

## Reference map

- [SKILL.md](SKILL.md) — the agent workflow and hard rules.
- [Design foundations](references/design-foundations.md) — Apple principles translated into implementation decisions.
- [Stability](references/stability.md) — state ownership, async cancellation, gestures, navigation, and interruption.
- [Motion and materials](references/motion-materials.md) — springs, transitions, Liquid Glass, blur, shaders, haptics, and fallbacks.
- [Platform layout](references/platform-layout.md) — iPhone, iPad, Mac, Watch, TV, and Vision layout differences.
- [Accessibility](references/accessibility.md) — Dynamic Type, VoiceOver, focus, contrast, and reduced effects.
- [Validation](references/validation.md) — hard gates, runtime abuse tests, visual matrix, performance, and the 100-point rubric.
- [Source index](references/source-index.md) — Apple documentation, WWDC sessions, and community research.

## Project layout

```text
SKILL.md
agents/openai.yaml
references/
scripts/audit_swiftui_ui.py
```

## Release hygiene

This repository contains portable Skill guidance and a dependency-free static audit only. Xcode projects, DerivedData, build products, simulator data, screenshots, archives, credentials, personal data, and machine-specific paths are intentionally excluded from version control.

## Contributing and community

Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request. Use [Discussions](https://github.com/ZHUOLIN0928/swiftui-interface-design-skill/discussions) for design questions, platform patterns, and profile feedback; use Issues for reproducible defects and focused feature requests.

## License

Released under the [MIT License](LICENSE).
