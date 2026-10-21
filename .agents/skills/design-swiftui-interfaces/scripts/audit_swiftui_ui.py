#!/usr/bin/env python3
"""Conservative static risk scan for SwiftUI interaction and visual-design code."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path


EXCLUDED_DIRS = {
    ".build",
    ".git",
    ".swiftpm",
    "Build",
    "DerivedData",
    "Packages",
    "Pods",
    "SourcePackages",
    "Tests",
    "UITests",
}

SEVERITY_ORDER = {"info": 0, "low": 1, "medium": 2, "high": 3}


@dataclass(frozen=True)
class Finding:
    severity: str
    rule: str
    file: str
    line: int
    message: str


@dataclass(frozen=True)
class Rule:
    severity: str
    rule: str
    pattern: re.Pattern[str]
    message: str


RULES = [
    Rule(
        "high",
        "adaptive.no-screen-bounds",
        re.compile(r"\bUIScreen\s*\.\s*main\s*\.\s*bounds\b"),
        "UIScreen.main.bounds hardcodes a global screen assumption; derive layout from the container.",
    ),
    Rule(
        "high",
        "identity.no-transient-uuid",
        re.compile(r"\.(?:id|tag)\s*\(\s*UUID\s*\(\s*\)\s*\)|\bid\s*:\s*UUID\s*\(\s*\)"),
        "A transient UUID breaks view, collection, or transition identity.",
    ),
    Rule(
        "medium",
        "interaction.semantic-control",
        re.compile(r"\.onTapGesture\b"),
        "Check whether this tap should be a Button, NavigationLink, Toggle, or another semantic control.",
    ),
    Rule(
        "medium",
        "animation.no-delay-state-machine",
        re.compile(r"\bDispatchQueue\s*\.\s*main\s*\.\s*asyncAfter\b|\bTask\s*\.\s*sleep\b"),
        "Delayed work near UI code can race rapid interaction; use explicit state, phases, or cancellable tasks.",
    ),
    Rule(
        "medium",
        "identity.foreach-indices",
        re.compile(r"\bForEach\s*\([^\n]*(?:\.indices|\.enumerated\s*\(\s*\))"),
        "Index-derived identity can break state and transitions when content changes.",
    ),
    Rule(
        "medium",
        "layout.fixed-frame",
        re.compile(r"\.frame\s*\([^\n]*(?:width|height)\s*:\s*(?:\d|CGFloat\s*\()"),
        "Review this fixed dimension at small containers and accessibility text sizes.",
    ),
    Rule(
        "medium",
        "layout.geometry-reader",
        re.compile(r"\bGeometryReader\s*\{"),
        "GeometryReader is valid but often overused; verify it does not create unstable or greedy layout.",
    ),
    Rule(
        "medium",
        "layout.ignores-safe-area",
        re.compile(r"\.ignoresSafeArea\s*\("),
        "Verify that only decorative/background content crosses safe areas and controls remain reachable.",
    ),
    Rule(
        "medium",
        "typography.hardcoded-size",
        re.compile(r"\.font\s*\(\s*\.system\s*\(\s*size\s*:"),
        "Hardcoded font sizes need Dynamic Type scaling or a documented display-only reason.",
    ),
    Rule(
        "medium",
        "color.hardcoded-rgb",
        re.compile(r"\bColor\s*\(\s*(?:red|hue|\.sRGB)|\bUIColor\s*\(\s*(?:red|hue)"),
        "Verify this hardcoded color across light, dark, increased contrast, and material backgrounds.",
    ),
    Rule(
        "medium",
        "motion.repeat-forever",
        re.compile(r"\.repeatForever\s*\("),
        "Repeated motion must pause offscreen/inactive and provide a Reduce Motion alternative.",
    ),
    Rule(
        "medium",
        "motion.raw-blur",
        re.compile(r"\.blur\s*\(\s*radius\s*:"),
        "Confirm blur communicates depth/focus, remains legible, and has a reduced-motion alternative if animated.",
    ),
    Rule(
        "low",
        "performance.any-view",
        re.compile(r"\bAnyView\s*\("),
        "Type erasure can hide identity/update costs; confirm it is necessary.",
    ),
    Rule(
        "low",
        "interaction.custom-gesture",
        re.compile(r"\.(?:gesture|highPriorityGesture|simultaneousGesture)\s*\("),
        "Document gesture priority, cancellation, scroll interaction, and accessibility alternative.",
    ),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Scan SwiftUI source for interaction, adaptation, motion, and material risks."
    )
    parser.add_argument("path", type=Path, help="Swift file or project directory")
    parser.add_argument(
        "--profile",
        choices=("standard", "expressive"),
        default="standard",
        help="Apply profile-specific project checks",
    )
    parser.add_argument("--json", action="store_true", help="Emit JSON")
    parser.add_argument(
        "--fail-on",
        choices=("none", "low", "medium", "high"),
        default="none",
        help="Return exit status 1 when a finding at or above this severity exists",
    )
    return parser.parse_args()


def swift_files(root: Path) -> list[Path]:
    if root.is_file():
        return [root] if root.suffix == ".swift" else []
    files = []
    for path in root.rglob("*.swift"):
        if not any(part in EXCLUDED_DIRS for part in path.parts):
            files.append(path)
    return sorted(files)


def relative(path: Path, root: Path) -> str:
    base = root if root.is_dir() else root.parent
    try:
        return str(path.relative_to(base))
    except ValueError:
        return str(path)


def animation_calls(text: str) -> list[tuple[int, str]]:
    """Return line/snippet pairs for balanced .animation(...) calls."""
    token = ".animation("
    calls: list[tuple[int, str]] = []
    search_from = 0
    while True:
        start = text.find(token, search_from)
        if start == -1:
            break
        open_paren = start + len(token) - 1
        depth = 0
        end = len(text)
        for index in range(open_paren, len(text)):
            character = text[index]
            if character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth == 0:
                    end = index + 1
                    break
        calls.append((text.count("\n", 0, start) + 1, text[start:end]))
        search_from = max(end, start + len(token))
    return calls


def scan_file(path: Path, root: Path) -> tuple[list[Finding], str]:
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        text = path.read_text(encoding="utf-8", errors="replace")
    findings: list[Finding] = []
    display = relative(path, root)
    for line_number, line in enumerate(text.splitlines(), start=1):
        if "swiftui-audit: ignore" in line:
            continue
        for rule in RULES:
            if rule.pattern.search(line):
                findings.append(
                    Finding(rule.severity, rule.rule, display, line_number, rule.message)
                )

    for line_number, call in animation_calls(text):
        compact = re.sub(r"\s+", "", call)
        if "value:" not in call and compact != ".animation(nil)":
            findings.append(
                Finding(
                    "high",
                    "animation.scope-value",
                    display,
                    line_number,
                    "Verify this animation is value-scoped; unscoped animation causes unrelated changes to animate.",
                )
            )
    return findings, text


def project_checks(
    texts: list[tuple[str, str]], profile: str
) -> list[Finding]:
    merged = "\n".join(text for _, text in texts)
    location = "<project>"
    findings: list[Finding] = []

    has_motion = bool(
        re.search(
            r"\bwithAnimation\b|\.animation\s*\(|\bPhaseAnimator\b|"
            r"\bKeyframeAnimator\b|\.scrollTransition\s*\(|\bTimelineView\b",
            merged,
        )
    )
    has_reduce_motion = "accessibilityReduceMotion" in merged
    if has_motion and not has_reduce_motion:
        findings.append(
            Finding(
                "high" if profile == "expressive" else "medium",
                "accessibility.reduce-motion",
                location,
                0,
                "Custom motion was found but no accessibilityReduceMotion adaptation was detected.",
            )
        )

    has_glass = bool(re.search(r"\.glassEffect\s*\(|\.buttonStyle\s*\(\s*\.glass", merged))
    has_reduce_transparency = "accessibilityReduceTransparency" in merged
    if has_glass and not has_reduce_transparency:
        findings.append(
            Finding(
                "high" if profile == "expressive" else "medium",
                "accessibility.reduce-transparency",
                location,
                0,
                "Liquid Glass was found but no accessibilityReduceTransparency fallback was detected.",
            )
        )

    glass_count = len(re.findall(r"\.glassEffect\s*\(", merged))
    container_count = len(re.findall(r"\bGlassEffectContainer\b", merged))
    if glass_count >= 3 and container_count == 0:
        findings.append(
            Finding(
                "high",
                "material.group-glass",
                location,
                0,
                f"{glass_count} glass effects were found without a GlassEffectContainer.",
            )
        )

    if "DragGesture" in merged and "@GestureState" not in merged:
        findings.append(
            Finding(
                "medium",
                "interaction.gesture-state",
                location,
                0,
                "DragGesture was found without GestureState; verify transient drag state is not duplicated in model state.",
            )
        )

    if ".repeatForever(" in merged and "scenePhase" not in merged:
        findings.append(
            Finding(
                "high" if profile == "expressive" else "medium",
                "performance.pause-ambient-motion",
                location,
                0,
                "Repeated motion was found without a scenePhase pause signal.",
            )
        )

    return findings


def main() -> int:
    args = parse_args()
    root = args.path.expanduser().resolve()
    if not root.exists():
        print(f"error: path does not exist: {root}", file=sys.stderr)
        return 2

    files = swift_files(root)
    if not files:
        print(f"error: no Swift files found under {root}", file=sys.stderr)
        return 2

    findings: list[Finding] = []
    texts: list[tuple[str, str]] = []
    for path in files:
        file_findings, text = scan_file(path, root)
        findings.extend(file_findings)
        texts.append((relative(path, root), text))
    findings.extend(project_checks(texts, args.profile))
    findings.sort(
        key=lambda item: (
            -SEVERITY_ORDER[item.severity],
            item.file,
            item.line,
            item.rule,
        )
    )

    counts = {
        severity: sum(1 for item in findings if item.severity == severity)
        for severity in ("high", "medium", "low", "info")
    }
    if args.json:
        print(
            json.dumps(
                {
                    "path": str(root),
                    "profile": args.profile,
                    "files_scanned": len(files),
                    "counts": counts,
                    "findings": [asdict(item) for item in findings],
                },
                indent=2,
                ensure_ascii=False,
            )
        )
    else:
        print(
            f"SwiftUI UI audit: {len(files)} files, profile={args.profile}, "
            f"high={counts['high']} medium={counts['medium']} low={counts['low']}"
        )
        for item in findings:
            suffix = f":{item.line}" if item.line else ""
            print(
                f"[{item.severity.upper():6}] {item.file}{suffix} "
                f"{item.rule} — {item.message}"
            )

    if args.fail_on != "none":
        threshold = SEVERITY_ORDER[args.fail_on]
        if any(SEVERITY_ORDER[item.severity] >= threshold for item in findings):
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
