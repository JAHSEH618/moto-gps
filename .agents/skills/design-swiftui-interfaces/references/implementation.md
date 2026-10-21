# SwiftUI implementation discipline

## Contents

- API truth
- Native-first selection
- State and composition
- Animation transactions
- Availability and fallbacks
- Preview and testing structure

## API truth

Before using an unfamiliar or release-specific symbol:

1. inspect the project deployment target;
2. inspect the selected Xcode/SDK version;
3. search the installed SDK interface or current Apple documentation;
4. confirm the exact signature, supported platforms, and availability;
5. compile a minimal use when practical;
6. add `#available` and a fallback when supporting older systems.

Do not trust a community snippet over the current SDK. Treat beta documentation as changeable.

## Native-first selection

Prefer:

- `NavigationStack` or `NavigationSplitView` for navigation;
- semantic controls and styles for interaction;
- `Toolbar` and platform placements for commands;
- `sheet`, `popover`, `alert`, `confirmationDialog`, and menus for their semantic roles;
- `ContentUnavailableView` or platform equivalents for empty states where available;
- semantic type styles, colors, and materials;
- system transitions, symbol effects, sensory feedback, and glass behavior.

Bridge to UIKit or AppKit when a required platform capability is missing, not to recreate a system component from scratch.

## State and composition

- Use observation appropriate to the deployment target and project conventions.
- Keep local ephemeral view state private.
- Inject models that are owned elsewhere.
- Extract subviews at stable identity and update boundaries.
- Keep view bodies declarative and free of I/O or expensive work.
- Use stable IDs for collections and transition sources.
- Avoid type erasure and identity-changing branches in hot paths unless necessary.

## Animation transactions

- Attach `.animation(_:value:)` to the smallest relevant subtree.
- Use `withAnimation` for explicit model events.
- Use transactions to suppress or alter animation for a specific change.
- Keep unrelated state changes out of the same animation transaction.
- Prefer phase/keyframe APIs for a bounded sequence over nested delayed tasks.
- Use gesture state for live tracking and commit model state on end.
- Make navigation and model updates atomic enough that the destination does not briefly render an impossible state.

## Availability and fallbacks

Fallbacks must preserve task, hierarchy, and accessibility even if they reduce visual richness.

Examples:

- custom Liquid Glass → standard material or semantic surface;
- source-linked navigation transition → automatic platform transition;
- shader atmosphere → static gradient or artwork-derived color;
- animated symbol → static symbol plus state label;
- rich parallax → no parallax;
- new toolbar behavior → conventional toolbar placement.

Avoid duplicating an entire screen solely for availability. Isolate version-specific presentation in modifiers or small components when possible.

## Preview and testing structure

Create representative previews or fixtures for:

- typical content;
- empty/loading/error;
- long text and right-to-left;
- smallest and largest containers;
- light and dark;
- accessibility text;
- Reduce Motion and Reduce Transparency equivalents;
- both profiles when the product supports both.

Keep tuning values centralized by semantic role, not in an unstructured global “magic numbers” file. For expressive hero motion, create a development-only tuning surface when several parameters must be adjusted together, then commit intentional defaults.
