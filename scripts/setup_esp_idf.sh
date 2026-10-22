#!/usr/bin/env bash
#
# Bootstrap ESP-IDF v5.5.5 for this project on macOS over a China network.
#
# What it does, in order:
#   1. clones esp-idf from Espressif's Gitee mirror        (fast, ~340 MB)
#   2. clones esp-gitee-tools                             (tiny)
#   3. points every submodule at gitee.com/esp-submodules (fast, ~600 MB)
#   4. fetches the one submodule Gitee has blocked (esp32-wifi-lib) through a
#      GitHub proxy, trying each proxy and retrying the flaky ones
#   5. installs the toolchain through Espressif's download CDN (~2.9 GB)
#   6. verifies idf.py / esptool versions
#
# Idempotent: every step is skipped if its result is already present, so
# re-running after a network failure resumes instead of starting over.
#
# Usage:
#   ./scripts/setup_esp_idf.sh            # full run
#   ./scripts/setup_esp_idf.sh --check    # environment checks only, no download
#
# Overridable via environment:
#   ESP_ROOT=~/esp        where esp-idf and the helper are cloned
#   IDF_VERSION=v5.5.5    tag to check out
#   USE_SSH=auto|1|0      Gitee transport; auto probes `ssh -T git@gitee.com`
#
set -euo pipefail
# A batch script must never sit on "Username for https://..." waiting for input.
# Wrong URLs then fail fast with an error instead of hanging the terminal.
export GIT_TERMINAL_PROMPT=0

IDF_VERSION="${IDF_VERSION:-v5.5.5}"
ESP_ROOT="${ESP_ROOT:-$HOME/esp}"
IDF_DIR="$ESP_ROOT/esp-idf"
TOOLS_DIR="$ESP_ROOT/esp-gitee-tools"
LOG="${LOG:-$ESP_ROOT/setup.log}"
USE_SSH="${USE_SSH:-auto}"
GITEE_SSH_PREFIX="git@gitee.com:"
GITEE_HTTPS_PREFIX="https://gitee.com/"
GITEE_PREFIX=""          # resolved by detect_transport()
GITEE_TRANSPORT=""
GITMODULES_PATCHED=0
WIFI_SUBMODULE="components/esp_wifi/lib"
# Tried in order. The blocked Gitee mirror is not in this list on purpose:
# gitee.com/esp-submodules/esp32-wifi-lib returns 403 "repository blocked".
GITHUB_PROXIES=(
  "https://gh-proxy.com/https://github.com"
  "https://ghfast.top/https://github.com"
  "https://github.com"
)
MIN_FREE_GIB=8
CHECK_ONLY=0
[[ "${1:-}" == "--check" ]] && CHECK_ONLY=1

# ---------------------------------------------------------------- output ----

BOLD=$'\033[1m'; DIM=$'\033[2m'; RED=$'\033[31m'; GREEN=$'\033[32m'; OFF=$'\033[0m'
step=0
say()  { printf '%s\n' "${BOLD}==>${OFF} $*"; }
note() { printf '%s\n' "    $*"; }
ok()   { printf '%s\n' "    ${GREEN}ok${OFF} $*"; }
warn() { printf '%s\n' "    ${DIM}skip${OFF} $*"; }
fail() { printf '%s\n' "${RED}error:${OFF} $*" >&2; exit 1; }
elapsed() { printf '%s' "$(( $(date +%s) - START ))"; }

START=$(date +%s)

# ------------------------------------------------------------- preflight ----

preflight() {
  say "Environment"
  [[ "$(uname -s)" == Darwin ]] || fail "this script targets macOS"
  command -v git >/dev/null || fail "git is missing"
  command -v python3 >/dev/null || fail "python3 is missing (ESP-IDF needs 3.9+)"
  command -v curl >/dev/null || fail "curl is missing"

  local py git_version
  py="$(python3 -c 'import sys; print("%d.%d" % sys.version_info[:2])')"
  note "python3 $py · $(git --version | awk '{print $3}') · $(uname -m)"

  python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 9) else 1)' \
    || fail "python3 >= 3.9 is required"

  local free_gib
  free_gib=$(df -g "$HOME" | awk 'NR==2 {print $4}')
  note "free disk: ${free_gib} GiB (need ~${MIN_FREE_GIB} GiB)"
  (( free_gib >= MIN_FREE_GIB )) || fail "not enough free disk space"

  local reach
  reach=$(curl -sS -o /dev/null -m 15 -w '%{http_code}' https://gitee.com || true)
  [[ "$reach" == 200 ]] || fail "gitee.com is not reachable (HTTP $reach)"
  ok "gitee reachable"
  note "log: $LOG"
}

# Gitee colours its banner ("You've \e[32msuccessfully\e[0m authenticated"), so
# the phrase is split by escape sequences in the raw stream. Strip them before
# matching, and match in the shell rather than with `grep -q`.
strip_ansi() {
  printf '%s' "$1" | sed $'s/\033\\[[0-9;]*m//g'
}

# A registered submodule whose clone failed keeps a .git directory and an empty
# worktree, and `git submodule status` reports it with a leading space — so the
# status flag cannot be trusted. Look at the worktree instead.
submodule_ready() {
  local dir="$IDF_DIR/$1" count
  [[ -d "$dir" ]] || return 1
  count=$(find "$dir" -mindepth 1 -maxdepth 1 ! -name .git 2>/dev/null | wc -l | tr -d ' ')
  (( count > 0 ))
}

submodule_paths() {
  git -C "$IDF_DIR" config -f "$IDF_DIR/.gitmodules" \
    --get-regexp '^submodule\..*\.path$' 2>/dev/null | awk '{print $2}'
}

missing_submodules() {
  local path
  while IFS= read -r path; do
    submodule_ready "$path" || printf '%s\n' "$path"
  done < <(submodule_paths)
}

# An interrupted `submodule update` leaves the submodule registered, HEAD
# intact and every blob already in .git/modules, but the worktree checked out
# as deletions — so `submodule status` calls it fine while the files are gone.
# The objects are local, so restoring them needs no network: reset each such
# worktree back to its own HEAD before touching the remotes again.
repair_submodules() {
  local path repaired=0
  while IFS= read -r path; do
    submodule_ready "$path" && continue
    [[ -e "$IDF_DIR/$path/.git" ]] || continue
    git -C "$IDF_DIR/$path" reset --hard HEAD -q 2>/dev/null || continue
    submodule_ready "$path" || continue
    note "repaired worktree: $path"
    repaired=$((repaired + 1))
  done < <(submodule_paths)
  (( repaired > 0 )) && ok "$repaired submodule worktrees restored from local objects"
  return 0
}

# Gitee serves both transports. SSH avoids the anonymous HTTPS rate limit; the
# probe reports success with exit code 1, so the text is what matters.
detect_transport() {
  case "$USE_SSH" in
    0) GITEE_PREFIX="$GITEE_HTTPS_PREFIX"; GITEE_TRANSPORT="https (forced)" ;;
    1) GITEE_PREFIX="$GITEE_SSH_PREFIX";   GITEE_TRANSPORT="ssh (forced)" ;;
    *)
      local probe
      probe=$(ssh -o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=accept-new \
                -T git@gitee.com 2>&1 || true)
      probe=$(strip_ansi "$probe")
      if [[ "$probe" == *"successfully authenticated"* ]]; then
        GITEE_PREFIX="$GITEE_SSH_PREFIX"; GITEE_TRANSPORT="ssh"
      else
        GITEE_PREFIX="$GITEE_HTTPS_PREFIX"; GITEE_TRANSPORT="https"
        note "ssh probe: $(printf '%s' "$probe" | grep -v '^Warning' | tail -1)"
      fi
      ;;
  esac
  note "gitee transport: $GITEE_TRANSPORT"
}

# ----------------------------------------------------------------- steps ----

clone_idf() {
  say "1/6 esp-idf $IDF_VERSION from Gitee (~340 MB)"
  if [[ -d "$IDF_DIR/.git" ]]; then
    local have
    have=$(git -C "$IDF_DIR" describe --tags --always 2>/dev/null || echo unknown)
    warn "already cloned ($have) — delete $IDF_DIR to force a fresh clone"
    return
  fi
  mkdir -p "$ESP_ROOT"
  git clone --depth 1 --branch "$IDF_VERSION" \
    "${GITEE_PREFIX}EspressifSystems/esp-idf.git" "$IDF_DIR"
  ok "cloned $(git -C "$IDF_DIR" rev-parse --short HEAD)"
}

clone_tools() {
  say "2/6 esp-gitee-tools (Espressif's own Gitee helper)"
  if [[ -d "$TOOLS_DIR/.git" ]]; then
    warn "already cloned"
    return
  fi
  git clone --depth 1 \
    "${GITEE_PREFIX}EspressifSystems/esp-gitee-tools.git" "$TOOLS_DIR"
  ok "cloned"
}

rewrite_submodules() {
  say "3/6 submodules → gitee.com/esp-submodules (~600 MB)"
  repair_submodules
  local before after
  before=$(missing_submodules | wc -l | tr -d ' ')
  note "missing before: $before"

  # The helper rewrites every submodule URL and then updates them. It stops at
  # esp32-wifi-lib, which Gitee has blocked; that is expected and handled next.
  [[ "$before" -gt 0 ]] || { warn "every submodule already present"; return; }

  ( cd "$IDF_DIR" && "$TOOLS_DIR/submodule-update.sh" ) \
    || note "the helper stopped early (expected: blocked esp32-wifi-lib)"

  after=$(missing_submodules | wc -l | tr -d ' ')
  ok "missing now: $after"

  # The helper writes https://gitee.com/... into .git/config. On the SSH
  # transport rewrite those to git@gitee.com:… here, locally, rather than with
  # a global insteadOf that would silently change every repo on the machine.
  if [[ "$GITEE_PREFIX" == "$GITEE_SSH_PREFIX" ]]; then
    local key url rewritten=0
    while IFS= read -r key; do
      url=$(git -C "$IDF_DIR" config --get "$key" || true)
      case "$url" in
        "${GITEE_HTTPS_PREFIX}"*)
          git -C "$IDF_DIR" config "$key" \
            "${GITEE_SSH_PREFIX}${url#"$GITEE_HTTPS_PREFIX"}"
          rewritten=$((rewritten + 1))
          ;;
      esac
    done < <(git -C "$IDF_DIR" config --name-only --get-regexp '^submodule\..*\.url$' || true)
    ok "$rewritten submodule URLs switched to ssh"
  fi
}

# The wifi submodule is ~30 MB of prebuilt archives behind a large history, and
# Gitee has blocked its mirror, so every git route here runs at tens of KB/s.
# jsDelivr serves the pinned blobs straight from its CDN, which is the only
# fast path. Only esp32s3/ is fetched: esp_wifi links lib/<target>/*.a for the
# target in use, and this project builds for esp32s3.
fetch_wifi_blobs() {
  local sha="$1" api list path size out base
  api="https://api.github.com/repos/espressif/esp32-wifi-lib/git/trees/$sha"
  list=$(curl -fsSL --max-time 30 "$api?recursive=1" 2>/dev/null | python3 -c '
import json, sys
for entry in json.load(sys.stdin).get("tree", []):
    if entry.get("type") == "blob" and entry.get("path", "").startswith("esp32s3/"):
        print(entry["path"], entry.get("size", 0))
' 2>/dev/null) || true
  [[ -n "$list" ]] || return 1

  while IFS= read -r line; do
    path=${line% *}
    size=${line##* }
    out="$IDF_DIR/$WIFI_SUBMODULE/$path"
    mkdir -p "$(dirname "$out")"
    [[ -f "$out" && "$(wc -c <"$out" | tr -d ' ')" == "$size" ]] && continue
    for base in "https://cdn.jsdelivr.net/gh/espressif/esp32-wifi-lib@$sha" \
                "https://raw.githubusercontent.com/espressif/esp32-wifi-lib/$sha"; do
      curl -fsSL --max-time 180 -o "$out" "$base/$path" || continue
      [[ "$(wc -c <"$out" | tr -d ' ')" == "$size" ]] && break
    done
    [[ -f "$out" && "$(wc -c <"$out" | tr -d ' ')" == "$size" ]] || return 1
  done <<< "$list"
  return 0
}

fetch_blocked_wifi_lib() {
  say "4/6 esp32-wifi-lib through a GitHub proxy (~200 MB)"
  if submodule_ready "$WIFI_SUBMODULE"; then
    warn "already present"
    return
  fi

  local sha
  sha=$(git -C "$IDF_DIR" ls-tree HEAD -- "$WIFI_SUBMODULE" | awk '{print $3}')
  note "pinned commit: ${sha:-unknown}"
  if [[ -n "$sha" ]] && fetch_wifi_blobs "$sha"; then
    ok "esp32s3 archives fetched directly ($(du -sh "$IDF_DIR/$WIFI_SUBMODULE" | cut -f1 | tr -d ' '))"
    # Leave the worktree alone from now on: the archives are in place, and a
    # clone would either hit the blocked mirror or read the same 30 MB again.
    git -C "$IDF_DIR" config "submodule.$WIFI_SUBMODULE.update" none
    return
  fi
  note "direct fetch failed — falling back to git through a proxy"

  local proxy attempt url
  for proxy in "${GITHUB_PROXIES[@]}"; do
    for attempt in 1 2; do
      url="$proxy/espressif/esp32-wifi-lib.git"
      note "try $attempt: $url"
      # .gitmodules is the authority here: `submodule update --init` copies it
      # into .git/config, so a config-side override is silently discarded and
      # git would fall back to the blocked gitee mirror. Patch the source entry
      # and let `sync` propagate it, then restore .gitmodules afterwards.
      git -C "$IDF_DIR" config -f "$IDF_DIR/.gitmodules" \
        "submodule.$WIFI_SUBMODULE.url" "$url"
      GITMODULES_PATCHED=1
      git -C "$IDF_DIR" submodule sync -- "$WIFI_SUBMODULE" >/dev/null 2>&1 || true
      rm -rf "${IDF_DIR:?}/$WIFI_SUBMODULE"
      # HTTP/1.1 matters: the default HTTP/2 stream gets reset on this repo.
      if git -C "$IDF_DIR" -c http.version=HTTP/1.1 -c http.postBuffer=524288000 \
           submodule update --init -- "$WIFI_SUBMODULE"; then
        ok "fetched via $proxy"
        return
      fi
      note "failed — retrying"
    done
  done
  fail "could not fetch $WIFI_SUBMODULE from any proxy"
}

finish_submodules() {
  say "5/6 remaining submodules (recursive)"
  git -C "$IDF_DIR" -c http.version=HTTP/1.1 -c http.postBuffer=524288000 \
    submodule update --init --recursive \
    || note "some submodule failed; the summary below lists what is missing"

  local missing
  missing=$(missing_submodules)
  if [[ -n "$missing" ]]; then
    printf '%s\n' "$missing" | sed 's/^/    missing: /'
    fail "submodules still missing — re-run this script to resume"
  fi
  ok "$(submodule_paths | wc -l | tr -d ' ') submodules ready"

  if [[ "$GITMODULES_PATCHED" == 1 ]] || ! git -C "$IDF_DIR" diff --quiet -- .gitmodules; then
    # Also heals a .gitmodules left patched by a run that died before this
    # step: the proxy URL is already in .git/config, so nothing is lost.
    # Keep the esp-idf checkout clean again; the proxy URL stays in .git/config
    # so an already-cloned submodule is never re-fetched from the blocked mirror.
    git -C "$IDF_DIR" checkout -- .gitmodules
    note "restored .gitmodules (proxy URL kept in .git/config)"
  fi
}

install_toolchain() {
  say "6/6 toolchain via Espressif CDN (~2.9 GB, slowest step)"
  if [[ -x "$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc" ]]; then
    warn "toolchain already installed"
    return
  fi
  # Two knobs matter here, and both are about the China network:
  #
  #   * host — dl.espressif.com measured 175 KB/s from here while its .cn
  #     twin served the same file at 13 MB/s, so the assets host is probed
  #     instead of assumed. IDF_GITHUB_ASSETS makes idf_tools rewrite every
  #     github.com URL to that host.
  #   * targets — `install.sh .` installs for all twelve SoCs (~2.9 GB).
  #     This project builds for esp32s3, so name it and skip the rest.
  local assets
  ensure_ca_bundle
  # idf_tools merges the requested targets with the ones saved in
  # idf-env.json, so a single earlier `install.sh .` pins all twelve SoCs here
  # forever. Drop the saved list so `esp32s3` really means esp32s3.
  if [[ -f "$HOME/.espressif/idf-env.json" ]]; then
    python3 - "$HOME/.espressif/idf-env.json" <<'PY'
import json, sys
path = sys.argv[1]
data = json.load(open(path))
changed = False
for record in data.get('idfInstalled', {}).values():
    if record.pop('targets', None) is not None:
        changed = True
if changed:
    json.dump(data, open(path, 'w'), indent=4)
PY
  fi
  assets=$(pick_assets_host)
  note "assets host: $assets"
  if ( cd "$IDF_DIR" && IDF_GITHUB_ASSETS="$assets" ./install.sh esp32s3 ); then
    ok "toolchain installed"
    return
  fi
  note "direct install failed — falling back to the Gitee helper (all targets, dl.espressif.com)"
  if ! ( cd "$IDF_DIR" && "$TOOLS_DIR/install.sh" . ); then
    note "helper failed — falling back to the documented mirror variable"
    ( cd "$IDF_DIR" && IDF_GITHUB_ASSETS=dl.espressif.com/github_assets \
        ./install.sh esp32s3 )
  fi
  ok "toolchain installed"
}

# python.org builds ship no CA file (etc/openssl/cert.pem is simply absent), so
# urllib fails every https download with "unable to get local issuer
# certificate" — idf_tools only papers over this for the two hosts it has a
# pinned root for. Point Python at a bundle that actually exists.
ensure_ca_bundle() {
  [[ -n "${SSL_CERT_FILE:-}" ]] && return 0
  local bundle
  bundle=$(python3 -c 'import certifi; print(certifi.where())' 2>/dev/null || true)
  [[ -n "$bundle" && -f "$bundle" ]] || bundle=/etc/ssl/cert.pem
  [[ -f "$bundle" ]] || return 0
  export SSL_CERT_FILE="$bundle"
  note "CA bundle: $bundle"
}

# Measures both Espressif asset hosts with a ranged request and returns the
# faster one, without the "https://" qualifier IDF_GITHUB_ASSETS rejects.
# A failed probe scores 0 so an unreachable host is never chosen. The probe
# report goes to stderr: this runs inside `$(...)`, so anything on stdout
# would be captured into the host name.
pick_assets_host() {
  local host probe best="" best_speed=0 speed
  probe="/github_assets/espressif/binutils-gdb/releases/download/esp-gdb-v17.1_20260402/xtensa-esp-elf-gdb-17.1_20260402-aarch64-apple-darwin24.5.tar.gz"
  for host in dl.espressif.cn dl.espressif.com; do
    speed=$(curl -s -o /dev/null -m 15 -r 0-3000000 -w '%{speed_download}' "https://$host$probe" 2>/dev/null || echo 0)
    speed=${speed%%.*}
    [[ "$speed" =~ ^[0-9]+$ ]] || speed=0
    note "  $host: $((speed / 1024)) KB/s" >&2
    if (( speed > best_speed )); then best="$host/github_assets"; best_speed=$speed; fi
  done
  [[ -n "$best" ]] || best="dl.espressif.com/github_assets"
  printf '%s' "$best"
}

verify() {
  say "Verify"
  local out
  out=$( cd "$IDF_DIR" && . ./export.sh >/dev/null 2>&1 \
         && idf.py --version 2>/dev/null \
         && python -m esptool version 2>/dev/null | head -1 ) || out=""
  if [[ -z "$out" ]]; then
    fail "idf.py could not run — read $LOG, then re-run this script"
  fi
  printf '%s\n' "$out" | sed 's/^/    /'
  [[ "$out" == *v5.5.5* ]] || fail "idf.py did not report v5.5.5"
  ok "ESP-IDF and esptool ready"
}

summary() {
  say "Done in $(elapsed)s"
  du -sh "$IDF_DIR" "$HOME/.espressif" 2>/dev/null | sed 's/^/    /'
  cat <<EOF

    Activate in every new terminal:

        . $IDF_DIR/export.sh

    Then build this project:

        cd $(cd "$(dirname "$0")/.." && pwd)
        idf.py -C platforms/esp32 set-target esp32s3
        idf.py -C platforms/esp32 build

EOF
}

# ------------------------------------------------------------------ main ----

mkdir -p "$ESP_ROOT"
exec > >(tee -a "$LOG") 2>&1

preflight
detect_transport
if (( CHECK_ONLY )); then
  say "Check only — nothing downloaded"
  exit 0
fi
clone_idf
clone_tools
rewrite_submodules
fetch_blocked_wifi_lib
finish_submodules
install_toolchain
verify
summary
