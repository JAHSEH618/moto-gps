# Contributing

Thanks for helping improve SwiftUI Interface Design Skill.

## Before opening a change

1. Keep the scope focused and preserve the standard/expressive profile model.
2. Verify SwiftUI API claims against current Apple documentation or the installed SDK; do not add guessed modifiers or parameters.
3. Do not add Apple logos, proprietary screenshots, redistributed Apple assets, secrets, personal data, private paths, or license-unclear code.
4. Keep stability, accessibility, platform adaptation, and reduced-effects behavior ahead of decorative polish.
5. Update the relevant reference and source index when changing a behavior contract or design rule.

## Local checks

From the project root:

```bash
python3 scripts/audit_swiftui_ui.py path/to/swiftui-project --profile standard
python3 -m py_compile scripts/*.py
```

If an Xcode project is available, also build the relevant schemes and record the exact SDK, device/simulator, accessibility settings, and runtime checks. Do not claim a build, frame rate, or assistive-technology result that was not measured.

## Pull requests

Explain what changed, why it changed, how it was tested, and what remains unverified. Keep generated Xcode output, simulator data, screenshots, archives, local caches, and machine-specific files out of commits.
