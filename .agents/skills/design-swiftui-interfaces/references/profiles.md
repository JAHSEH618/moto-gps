# Experience profiles

## Contents

- Shared foundation
- Standard profile
- Expressive profile
- Selection rules
- Intensity budget

## Shared foundation

Both profiles must use the same information architecture, control semantics, state model, accessibility behavior, and task completion path. A profile may change presentation and choreography, but never make basic interaction less predictable.

## Standard profile (`standard`, 常规版)

Use by default and for productivity, settings, finance, health, education, enterprise, dense data, or any request that does not explicitly ask for a bold identity.

### Character

- calm, clear, precise, familiar;
- content-led rather than effect-led;
- system components and materials;
- one restrained accent color;
- low-amplitude, short, purposeful motion;
- subtle haptic feedback only for meaningful commits.

### Motion language

- use the system’s automatic transitions when they communicate hierarchy correctly;
- use critically damped or lightly elastic springs;
- use source-linked zoom or matched geometry only when a source-destination relationship matters;
- stagger only tightly related elements and keep the sequence brief;
- stop ambient motion when it is not adding status or context.

### Material language

- allow standard controls and navigation chrome to adopt current system materials;
- use standard materials in the content layer;
- avoid colorful glass unless it communicates an accent or state;
- prefer scroll-edge separation over custom persistent dividers where supported.

### Failure pattern to avoid

Do not turn “standard” into static or generic. Preserve responsive press states, smooth navigation, spatial continuity, and small moments of delight.

## Expressive profile (`expressive`, 个性版)

Use only when explicitly requested for a flagship, media, creative, entertainment, launch, immersive, or strongly branded experience.

### Character

- cinematic but controlled;
- colorful and dimensional;
- strong source-destination continuity;
- responsive Liquid Glass clusters;
- deliberate atmospheric graphics;
- richer synchronized visual, haptic, and content transitions.

### Allowed hero techniques

Choose a small subset:

- a glass control cluster that merges and separates through a shared container;
- a selected item that expands into detail through a source-linked transition;
- scroll-reactive navigation chrome that reveals depth without hiding content;
- a full-bleed color field or artwork-derived atmosphere behind the content layer;
- a shader-backed distortion, light field, or blur used as a bounded hero effect;
- device- or pointer-coupled parallax with low amplitude and immediate settling;
- a phase/keyframe sequence for a meaningful completion or reveal;
- synchronized haptic feedback at snap, commit, success, or state transition.

### Composition rules

- Keep one dominant effect per screen.
- Keep one hero transition per task flow.
- Maintain a quiet content layer around the hero effect.
- Let colored glass indicate prominence or identity; do not tint every control.
- Use glass for controls and navigation, not as a replacement for all backgrounds.
- Treat blur as a depth or focus transition, not a generic beauty filter.
- Materialize glass through lensing/shape/scale continuity rather than opacity alone.
- Make every ambient effect pause, reduce, or become static when appropriate.

### Expressive does not permit

- perpetual motion behind reading content;
- large parallax on every scroll;
- multiple unrelated springs firing together;
- rainbow tints without hierarchy;
- blur that destroys text or control contrast;
- custom gestures that replace expected navigation without a familiar alternative;
- large shader surfaces without device testing and a fallback;
- motion that delays task completion.

## Selection rules

- If the user says “Apple-like,” “native,” “clean,” or “polished,” choose `standard`.
- If the user says “bold,” “cinematic,” “colorful Liquid Glass,” “rich motion,” “very personal,” “酷/帅/华丽,” choose `expressive`.
- If the request is to fix an interaction bug, use `standard` until the bug is resolved. Reapply the project’s expressive layer afterward.
- If a product contains both, use `standard` for routine flows and reserve `expressive` for onboarding, discovery, creation, playback, progress, or completion moments.

## Intensity budget

Start with ranges, then tune in interactive previews instead of treating numbers as law.

| Property | Standard starting range | Expressive starting range |
| --- | --- | --- |
| Spring bounce | none to subtle | subtle to moderate |
| Transition scale | nearly 1:1 | visibly dimensional, never disorienting |
| Stagger | none or very short | short and rhythmically grouped |
| Parallax | usually none | low amplitude, direct, quickly settling |
| Glass tint | semantic accent only | selective brand/hero tint |
| Blur/shader | hierarchy support | bounded atmospheric or hero effect |
| Haptics | commit/error/snap | commit plus selected hero moments |

If several properties are at the expressive end simultaneously, reduce at least one. Contrast creates personality more effectively than maximum intensity everywhere.
