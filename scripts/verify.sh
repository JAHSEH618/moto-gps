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
install_hook=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-ios) run_ios=0; shift ;;
    --install-hook) install_hook=1; shift ;;
    --help|-h)
      sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'
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
  cmake -S . -B build/native -DMOTO_BUILD_WEB=OFF -DMOTO_BUILD_TESTS=ON
  cmake --build build/native --parallel 4
  ctest --test-dir build/native --output-on-failure
}

test_swift() {
  swift test --package-path platforms/ios
}

build_ios() {
  if ! command -v xcodegen >/dev/null 2>&1; then
    echo "xcodegen is not installed; skipping the app build"
    echo "(install it with 'brew install xcodegen' to cover the one surface"
    echo " the Ubuntu CI job cannot see)"
    return 0
  fi
  xcodegen generate --spec platforms/ios/project.yml
  xcodebuild -project platforms/ios/MotoGPS.xcodeproj -scheme MotoGPS \
    -sdk iphonesimulator -configuration Debug \
    -derivedDataPath build/ios-verify \
    CODE_SIGNING_ALLOWED=NO build
}

step_run "todo log" verify_todo_log
step_run "native build and tests" build_native
step_run "Swift package tests" test_swift
if [[ "$run_ios" == "1" ]]; then
  step_run "iOS app build" build_ios
fi

printf '\n==> summary\n'
for index in "${!names[@]}"; do
  printf '    %-24s %ss\n' "${names[$index]}" "${durations[$index]}"
done

if [[ "$failures" -ne 0 ]]; then
  printf '\n%d step(s) failed; logs in %s\n' "$failures" "$log_dir"
  exit 1
fi
printf '\nall steps green\n'
