#!/bin/bash
# Run every check the repository expects, locally.
#
# The workflows in .github/workflows are the documented gate, but this fork has
# no Actions enabled: `gh workflow list` on origin returns nothing, so a green
# badge has not existed for this branch at all. This script is the gate that
# actually runs, and `--install-hook` wires it into pre-push so a broken slice
# cannot leave the machine.
#
# Usage:
#   scripts/verify.sh                 run everything
#   scripts/verify.sh --no-ios        skip the iOS app build (native + Swift only)
#   scripts/verify.sh --esp32         also build the firmware with ESP-IDF
#                                     (IDF_PATH, ~/esp/esp-idf, or ../esp/esp-idf)
#   scripts/verify.sh --install-hook  install .git/hooks/pre-push, then verify
#   scripts/verify.sh --help
#
# Exit: 0 all green, 1 the first failing step (its log is printed).

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"
log_dir="$repo_root/build/verify"
mkdir -p "$log_dir"

run_ios=1
run_esp32=0
install_hook=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-ios) run_ios=0; shift ;;
    --esp32) run_esp32=1; shift ;;
    --install-hook) install_hook=1; shift ;;
    --help|-h)
      sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *) echo "unknown argument: $1" >&2; exit 1 ;;
  esac
done

if [[ "$install_hook" == "1" ]]; then
  hook="$repo_root/.git/hooks/pre-push"
  if [[ -e "$hook" ]] && ! grep -q "scripts/verify.sh" "$hook" 2>/dev/null; then
    echo "refusing to overwrite $hook: it exists and is not ours" >&2
    exit 1
  fi
  mkdir -p "$(dirname "$hook")"
  cat > "$hook" <<'HOOK'
#!/bin/bash
# Installed by scripts/verify.sh --install-hook.
# Bypass with `git push --no-verify` when the failure is unrelated.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
exec scripts/verify.sh
HOOK
  chmod +x "$hook"
  echo "installed $hook"
fi

step=0
failures=0
declare -a names=()
declare -a durations=()

step_run() {
  local name="$1"
  shift
  step=$((step + 1))
  local log="$log_dir/$(printf '%02d' "$step")-$(echo "$name" | tr ' /' '--').log"
  printf '\n==> %s\n' "$name"
  local start=$SECONDS
  if "$@" > "$log" 2>&1; then
    local elapsed=$((SECONDS - start))
    printf '    ok (%ds)\n' "$elapsed"
  else
    local elapsed=$((SECONDS - start))
    printf '    FAILED (%ds) — %s\n' "$elapsed" "$log"
    tail -30 "$log" | sed 's/^/    /'
    failures=$((failures + 1))
  fi
  names+=("$name")
  durations+=("$((SECONDS - start))")
}

verify_todo_log() {
  node scripts/todo_status.mjs --check
}

build_native() {
  # `set -e` does not apply to a function called from `if`, so every command
  # that can fail has to stop the step itself: a failed compile followed by a
  # green ctest over stale binaries is how this gate once passed a broken UI.
  cmake -S . -B build/native -DMOTO_BUILD_WEB=OFF -DMOTO_BUILD_TESTS=ON || return 1
  cmake --build build/native --parallel 4 || return 1
  # tests/ is not tracked; a checkout without it still has to compile.
  if [[ ! -d tests/native ]]; then
    echo "tests/ is not in this checkout; built the shared core, ran no tests"
    return 0
  fi
  ctest --test-dir build/native --output-on-failure || return 1
}

test_swift() {
  swift test --package-path platforms/ios
}

build_ios() {
  # Command Line Tools cannot build an app; use the installed Xcode if the
  # active developer directory is the CLT instance.
  if ! xcodebuild -version >/dev/null 2>&1 && [[ -d /Applications/Xcode.app ]]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
  fi
  if ! command -v xcodegen >/dev/null 2>&1; then
    echo "xcodegen is not installed; skipping the app build"
    echo "(install it with 'brew install xcodegen' to cover the one surface"
    echo " the Ubuntu CI job cannot see)"
    return 0
  fi
  xcodegen generate --spec platforms/ios/project.yml || return 1
  xcodebuild -project platforms/ios/MotoGPS.xcodeproj -scheme MotoGPS \
    -sdk iphonesimulator -configuration Debug \
    -derivedDataPath build/ios-verify \
    CODE_SIGNING_ALLOWED=NO build || return 1
}

# The only check that compiles the firmware at all. A shared source missing
# from the ESP-IDF component, or an xtensa-only warning promoted to an error,
# is invisible to the native build.
build_esp32() {
  local idf=""
  for candidate in "${IDF_PATH:-}" "$HOME/esp/esp-idf" "$repo_root/../esp/esp-idf"; do
    if [[ -n "$candidate" && -f "$candidate/export.sh" ]]; then
      idf="$candidate"
      break
    fi
  done
  if [[ -z "$idf" ]]; then
    echo "ESP-IDF not found (set IDF_PATH or install it with scripts/setup_esp_idf.sh)"
    return 1
  fi
  # export.sh is written for bash; sourcing it from zsh leaves idf.py off PATH.
  bash -c ". '$idf/export.sh' >/dev/null && idf.py -C platforms/esp32 build" || return 1
  # Release overlay (CONFIG_MOTO_TLOG_DISABLE=y) in its own build dir, so the
  # disabled branches keep compiling. A saved sdkconfig wins over the
  # defaults files and a Kconfig edit does not trigger a reconfigure, so the
  # release config is regenerated every time and then checked.
  rm -f platforms/esp32/build-release/sdkconfig
  bash -c ". '$idf/export.sh' >/dev/null && idf.py -C platforms/esp32 \
    -B platforms/esp32/build-release \
    -DSDKCONFIG=build-release/sdkconfig \
    -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.release' \
    reconfigure build" || return 1
  if ! grep -q '^#define CONFIG_MOTO_TLOG_DISABLE 1' \
      platforms/esp32/build-release/config/sdkconfig.h; then
    echo "release build does not have CONFIG_MOTO_TLOG_DISABLE set"
    return 1
  fi
}

verify_app_size() {
  scripts/check_app_size.sh
}

step_run "todo log" verify_todo_log
step_run "native build and tests" build_native
step_run "Swift package tests" test_swift
if [[ "$run_ios" == "1" ]]; then
  step_run "iOS app build" build_ios
fi
if [[ "$run_esp32" == "1" ]]; then
  step_run "ESP32 firmware build" build_esp32
fi
# After the firmware build, so the gate measures the image just built.
step_run "app size gate" verify_app_size

printf '\n==> summary\n'
for index in "${!names[@]}"; do
  printf '    %-24s %ss\n' "${names[$index]}" "${durations[$index]}"
done

if [[ "$failures" -ne 0 ]]; then
  printf '\n%d step(s) failed; logs in %s\n' "$failures" "$log_dir"
  exit 1
fi
printf '\nall steps green\n'
