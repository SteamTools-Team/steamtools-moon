#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
#
# scan-all.sh — proactive SteamStub bypass for every AddedApp.
#
# The runtime hook (feats/steamstub.cpp::onLaunchApp) processes a
# wrapped exe the first time the user clicks Play. That works, but:
#   - it adds a ~5-30 s delay on the very first launch of a fresh
#     install,
#   - it does nothing if Steam was started with an older SteamTools.so
#     that lacked v3 support — the user may not realise a restart is
#     needed before clicking Play.
#
# This script does the same scan + unpack offline. Idempotent: an
# exe with an up-to-date `.steamless_done` marker is skipped, and an
# exe whose mtime is newer than the marker is reprocessed (handles
# Steam game updates that re-wrap the binary).
#
# Usage:
#   scan-all.sh                       # scan every AddedApp from
#                                     # ~/.config/SteamTools/config.yaml
#   scan-all.sh <appid> [<appid> ...] # only the listed appids
#
# Exit code is 0 if every wrapped exe encountered was processed
# successfully (or already up to date). Non-zero if at least one
# helper invocation failed.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_HELPER="$SCRIPT_DIR/run-steamless.sh"
CONFIG_FILE="$HOME/.config/SteamTools/config.yaml"

log()  { echo "[scan-all] $*"; }
warn() { echo "[scan-all] WARN: $*" >&2; }

[ -x "$RUN_HELPER" ] || { warn "missing helper: $RUN_HELPER"; exit 1; }

# Resolve STEAMLESS_HOME the same way the runtime path does, so
# manual invocations don't depend on the user knowing about it.
if [ -z "${STEAMLESS_HOME:-}" ]; then
    for cand in \
        "$SCRIPT_DIR/../steamless-bin" \
        "$HOME/.local/share/SteamTools/steamless-bin"
    do
        if [ -f "$cand/Steamless.CLI.exe" ]; then
            STEAMLESS_HOME="$(cd "$cand" && pwd)"
            break
        fi
    done
fi
if [ -z "${STEAMLESS_HOME:-}" ] || [ ! -f "$STEAMLESS_HOME/Steamless.CLI.exe" ]; then
    warn "Steamless.CLI.exe not found; run tools/steamstub-bypass/install-steamless.sh"
    exit 1
fi
export STEAMLESS_HOME

# ── 1. assemble target appid list ──────────────────────────────────────
declare -a APP_IDS
if [ "$#" -gt 0 ]; then
    APP_IDS=("$@")
elif [ -f "$CONFIG_FILE" ]; then
    # Pull AdditionalApps from config.yaml.  Pure POSIX-ish parsing —
    # we don't want a yaml dependency.  Lines look like:
    #   AdditionalApps:
    #     - 12345  # comment
    #     - 67890
    while IFS= read -r line; do
        APP_IDS+=("$line")
    done < <(awk '
        /^AdditionalApps:/ { in_section=1; next }
        in_section && /^[A-Za-z_][A-Za-z0-9_]*:/ { in_section=0 }
        in_section {
            sub(/#.*/, "", $0)
            sub(/^[[:space:]]*-[[:space:]]*/, "", $0)
            gsub(/[[:space:]]/, "", $0)
            if ($0 ~ /^[0-9]+$/) print $0
        }
    ' "$CONFIG_FILE")
else
    warn "no appids passed and no $CONFIG_FILE; nothing to do"
    exit 0
fi

if [ "${#APP_IDS[@]}" -eq 0 ]; then
    log "no AddedApps in config; nothing to scan"
    exit 0
fi

# ── 2. resolve install dir for each appid ──────────────────────────────
# Steam can install across multiple library roots.  Probe the usual
# suspects + every path declared in libraryfolders.vdf.
collect_steam_roots() {
    for root in \
        "$HOME/.steam/steam" \
        "$HOME/.steam/debian-installation" \
        "$HOME/.local/share/Steam"
    do
        [ -d "$root/steamapps" ] && echo "$root/steamapps"
        local libvdf="$root/steamapps/libraryfolders.vdf"
        [ -f "$libvdf" ] || continue
        # Crude key-extraction: any "path" line. Mirrors the C++
        # findInstallDir() helper exactly so behaviour matches.
        grep -oE '"path"[[:space:]]*"[^"]+"' "$libvdf" 2>/dev/null \
            | sed -E 's/.*"([^"]+)"$/\1/' \
            | while IFS= read -r extra; do
                [ -d "$extra/steamapps" ] && echo "$extra/steamapps"
              done
    done | awk '!seen[$0]++'  # dedupe, preserve order
}

mapfile -t LIB_ROOTS < <(collect_steam_roots)
if [ "${#LIB_ROOTS[@]}" -eq 0 ]; then
    warn "no Steam library roots found"
    exit 1
fi

resolve_install_dir() {
    local appid="$1"
    for libroot in "${LIB_ROOTS[@]}"; do
        local manifest="$libroot/appmanifest_${appid}.acf"
        [ -f "$manifest" ] || continue
        local installdir
        installdir=$(grep -oE '"installdir"[[:space:]]*"[^"]+"' "$manifest" \
            | head -1 | sed -E 's/.*"([^"]+)"$/\1/')
        if [ -n "$installdir" ] && [ -d "$libroot/common/$installdir" ]; then
            echo "$libroot/common/$installdir"
            return 0
        fi
    done
    return 1
}

# ── 3. for each app, walk install dir, hand each .exe to run-steamless ─
TOTAL=0
PROCESSED=0
SKIPPED=0
FAILED=0

for appid in "${APP_IDS[@]}"; do
    install_dir=$(resolve_install_dir "$appid") || {
        log "appid=$appid: not installed; skipping"
        continue
    }
    log "appid=$appid: scanning $install_dir"

    # Walk depth-3 max — same cap the runtime hook uses to avoid
    # runaway recursion in unusual install layouts.
    while IFS= read -r -d '' exe; do
        TOTAL=$((TOTAL+1))

        # Skip the helper's own artefacts: <exe>.original.exe is the
        # backup left behind, <exe>.unpacked.exe is a transient
        # output. Reprocessing them recursively corrupts the backup
        # chain (we hit this once in the wild).
        case "$(basename "$exe")" in
            *.original.exe|*.unpacked.exe) SKIPPED=$((SKIPPED+1)); continue ;;
        esac

        # Skip already-processed exes whose marker is fresh.
        marker="$exe.steamless_done"
        if [ -f "$marker" ] && [ "$marker" -nt "$exe" ]; then
            SKIPPED=$((SKIPPED+1))
            continue
        fi
        # Stale marker (exe rewritten by a Steam update) — drop so
        # the helper reprocesses.
        [ -f "$marker" ] && rm -f "$marker"

        # Defer the heavy detect/unpack to run-steamless.sh — same
        # path the runtime hook takes, so we can't drift.
        if QUIET=1 "$RUN_HELPER" "$exe" >/dev/null 2>&1; then
            rc=0
        else
            rc=$?
        fi
        case "$rc" in
            0) PROCESSED=$((PROCESSED+1)); log "  unpacked: $(basename "$exe")" ;;
            2) SKIPPED=$((SKIPPED+1));   ;;  # not stub-wrapped, no work
            *) FAILED=$((FAILED+1)); warn "  helper rc=$rc on $exe" ;;
        esac
    done < <(find "$install_dir" -maxdepth 4 -type f -iname "*.exe" -print0)
done

log "summary: scanned=$TOTAL processed=$PROCESSED skipped=$SKIPPED failed=$FAILED"
[ "$FAILED" -eq 0 ]
