# Accessibility and inclusive motion

## Contents

- Semantic access
- Text and layout
- Visual communication
- Motion and transparency
- Input and focus
- Verification

## Semantic access

- Use semantic controls so activation, role, state, keyboard behavior, and VoiceOver are available by default.
- Give icon-only controls concise labels.
- Expose value, state, hint, and custom actions only when they add information.
- Group or contain children according to reading intent; do not flatten meaningful structure.
- Keep accessibility focus stable across updates and move it deliberately after major presentation changes.
- Announce asynchronous completion or errors when the visual update is not sufficient.

## Text and layout

- Use semantic text styles and support Dynamic Type.
- Let rows and controls grow vertically.
- Avoid truncating task-critical labels.
- Reflow dense horizontal layouts at large sizes.
- Keep tap targets at least the platform-recommended size; Apple’s iOS design guidance uses 44 by 44 points as a baseline.
- Verify custom fonts remain readable at increased sizes and bold text.

## Visual communication

- Meet contrast needs in every appearance and over moving/material backgrounds.
- Never use color alone to communicate selection, validation, progress, or status.
- Add a glyph, shape, label, pattern, or position cue.
- Keep text and fine symbols on sufficiently separated material.
- Verify focus, pressed, disabled, selected, loading, error, and destructive states.

## Motion and transparency

Read environment accessibility values instead of inventing an app-only accessibility toggle.

With Reduce Motion:

- reduce automatic/repetitive motion;
- remove large zoom, parallax, z-axis, and peripheral motion;
- tighten springs and remove bounce;
- replace translation with fades or immediate changes;
- keep direct gesture tracking when it improves control;
- avoid animating into and out of blur.

With Reduce Transparency:

- make primarily translucent backgrounds opaque;
- keep hierarchy through semantic surfaces and borders;
- ensure text contrast does not depend on the sampled background.

With Differentiate Without Color and Increase Contrast:

- add redundant noncolor cues;
- increase separation and stroke/label clarity as needed.

## Input and focus

- Support switch control and Voice Control through semantic labels and actions.
- Support keyboard navigation and shortcuts on iPadOS/macOS where appropriate.
- Keep focus order aligned with visual and task order.
- Do not require precision dragging for the only way to complete a task.
- Provide alternatives to hover, motion, haptics, and multi-finger gestures.

## Verification

Test:

- VoiceOver reading and actions;
- largest accessibility text sizes;
- bold text;
- Reduce Motion;
- Reduce Transparency;
- Increase Contrast;
- Differentiate Without Color;
- grayscale/color filters where relevant;
- keyboard-only and switch-like navigation for supported platforms;
- Accessibility Inspector audits when full Xcode is available.

An interface that looks correct in a screenshot but fails these checks cannot pass the quality gate.
