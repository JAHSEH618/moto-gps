#!/bin/bash
# Report whether a booted simulator runtime can draw Chinese at all.
#
# On some runtimes the system Chinese face is registered but cannot be loaded:
# PingFang resolves to its own .ttc and then falls back to LastResort, so every
# Han character renders as a box in every app, including SpringBoard. That is a
# runtime defect, not an app bug, and no amount of bundling or CTFontManager
# registration fixes it — the zh-Hans cascade is pinned to PingFang and a
# substitute with the same PostScript name is refused.
#
# A screenshot review of Chinese copy on such a device is meaningless. Run this
# first and treat exit code 2 as "layout only, do not judge typography".
#
# Usage: scripts/sim_font_gate.sh [--device <udid|name>]
# Exit:  0 Chinese renders, 1 setup problem, 2 Chinese unusable on this runtime.

set -euo pipefail

device=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --device|-d)
      [[ $# -ge 2 ]] || { echo "--device needs a value" >&2; exit 1; }
      device="$2"
      shift 2
      ;;
    --help|-h)
      sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

workdir="${TMPDIR:-/tmp}/moto-sim-font-gate"
mkdir -p "$workdir"

xcrun simctl list devices -j > "$workdir/devices.json"
MOTO_GATE_NEEDLE="$device" python3 - "$workdir/devices.json" > "$workdir/device.tsv" <<'PY'
import json, os, sys

needle = os.environ.get("MOTO_GATE_NEEDLE", "")
with open(sys.argv[1], encoding="utf-8") as handle:
    devices = json.load(handle)["devices"]

matches = []
for runtime, entries in devices.items():
    for entry in entries:
        if needle:
            if entry["udid"] == needle or entry["name"] == needle:
                matches.append((entry, runtime))
        elif entry.get("state") == "Booted":
            matches.append((entry, runtime))

preferred = [pair for pair in matches if "iPhone" in pair[0]["name"]] or matches
if not preferred:
    hint = "no booted simulator; boot one first, or pass --device <udid>" if not needle \
        else f"no simulator matches {needle!r}"
    raise SystemExit(hint)

entry, runtime = preferred[0]
print(entry["udid"], entry["name"],
      runtime.replace("com.apple.CoreSimulator.SimRuntime.", ""), sep="\t")
PY

IFS=$'\t' read -r udid name runtime < "$workdir/device.tsv"

probe_src="$workdir/font_probe.m"
probe_bin="$workdir/font_probe"

cat > "$probe_src" <<'OBJC'
// Print the glyph count of the system Chinese face and whether a Han character
// reaches a real glyph. 7 glyphs means LastResort: the face failed to load.
#import <Foundation/Foundation.h>
#import <CoreText/CoreText.h>

int main(void) {
  @autoreleasepool {
    CTFontRef font = CTFontCreateWithName(CFSTR("PingFangSC-Regular"), 17, NULL);
    if (font == NULL) {
      printf("glyphs=0 han=0\n");
      return 0;
    }
    UniChar han = 0x4E2D;  // 中
    CGGlyph glyph = 0;
    const bool mapped = CTFontGetGlyphsForCharacters(font, &han, &glyph, 1);
    printf("glyphs=%zu han=%d glyph=%u\n", (size_t)CTFontGetGlyphCount(font),
           mapped ? 1 : 0, (unsigned)glyph);
    CFRelease(font);
  }
  return 0;
}
OBJC

# The probe is built against the simulator SDK and executed inside the device
# by simctl, which supplies the runtime's dyld root. Rebuilding takes about a
# second; a cached binary would go stale the moment the SDK changes.
xcrun --sdk iphonesimulator clang -fobjc-arc \
  -target arm64-apple-ios17.0-simulator \
  -isysroot "$(xcrun --sdk iphonesimulator --show-sdk-path)" \
  -framework Foundation -framework CoreText \
  -o "$probe_bin" "$probe_src"

output="$(xcrun simctl spawn "$udid" "$probe_bin" 2>/dev/null | tail -1)"
if [[ ! "$output" =~ glyphs=([0-9]+)[[:space:]]+han=([01]) ]]; then
  echo "could not read the font probe result from $name: '${output}'" >&2
  exit 1
fi
glyphs="${BASH_REMATCH[1]}"
han="${BASH_REMATCH[2]}"

echo "$name ($runtime): PingFangSC-Regular $glyphs glyphs, 中 mapped=$han"

# A real system Chinese face carries ~47k glyphs; 10k is far above
# LastResort (7) and far below any real face, so it cannot be a false pass.
if [[ "$han" == "1" && "$glyphs" -ge 10000 ]]; then
  exit 0
fi

cat >&2 <<EOF
layout-only: this runtime cannot load the system Chinese face.
  PingFangSC-Regular reports $glyphs glyphs here. The LastResort fallback has 7
  and a real PingFang about 47000, so every Han character renders as a box —
  in this app and in SpringBoard alike.
  Chinese copy and typography cannot be reviewed on this device.
  Use a runtime whose probe passes (iOS 27.0 and 27.1 do), or keep this device
  for layout only.
EOF
exit 2
