#!/bin/bash
# App partition budget gate (v2 r1 §11.3, review condition).
#
# The v2 partition plan gives each OTA app slot 6 MiB out of 32 MB. A firmware
# that fills 90% of it leaves no room for an OTA image plus the assets/tlog
# regions that must coexist, so the gate fails above that line instead of at
# link time.
#
# The table now has app0/app1 at 6 MiB each. Re-cutting it requires a USB
# full flash (nvs offset is preserved, so calibration survives).
set -euo pipefail

binary="${1:-platforms/esp32/build/moto_gps_esp32.bin}"
partition_bytes="${2:-6291456}"   # 6 MiB (r1 partition plan)
percent="${3:-90}"

if [[ ! -f "$binary" ]]; then
  echo "no firmware image at $binary (run: idf.py -C platforms/esp32 build)"
  exit 0
fi

size=$(stat -f%z "$binary" 2>/dev/null || stat -c%s "$binary")
limit=$((partition_bytes * percent / 100))
used=$((size * 100 / partition_bytes))
printf 'app %d bytes = %d%% of %d bytes (%d%% budget)\n' \
  "$size" "$used" "$partition_bytes" "$percent"
if (( size > limit )); then
  echo "FAIL: firmware exceeds the ${percent}% app-budget (limit ${limit} bytes)" >&2
  exit 1
fi
