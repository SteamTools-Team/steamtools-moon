#!/usr/bin/env bash
# ============================================================================
# steamtools-moon auto-fix
#
# Runs when Lumen detects that steamtools-moon did NOT inject into the current
# Steam session (the "Buy" button shows on games added via LuaTools). It:
#   1. downloads the LATEST steamtools-moon (Lumen) release asset,
#   2. runs its setup.sh install — which stops Steam, installs the library and
#      repairs EVERY *steam*.desktop launcher on the system (prompting for the
#      sudo password here in the terminal for the system-wide entries),
#   3. relaunches Steam through the injected wrapper.
#
# Self-contained: fetched + run via `curl -fsSL <raw>/autofix.sh | bash`, so it
# must not depend on any sibling file. Only touches steamtools-moon (not Lumen or
# the LuaTools plugin).
#
# Served from the repo's raw branch URL, so fixes go live without a release cut.
# ============================================================================
set -u

REPO="SteamTools-Team/steamtools-moon"
# Asset name matcher. jq's test() sees the parsed .name (no surrounding quotes),
# so it uses '.*'; the grep fallback scans raw JSON, so it uses '[^"]*' to avoid
# crossing a quote. Both use '[.]' for a literal dot (a bare '\.' is not a valid
# jq string escape and a raw '"' inside a jq "..." string breaks the parse).
ASSET_RE_JQ='steamtools-moon-linux-.*-lumen[.]zip'
ASSET_RE_GREP='steamtools-moon-linux-[^"]*-lumen[.]zip'
SLSDIR="$HOME/.local/share/SteamTools"
WRAPPER="$SLSDIR/path/steam"

# ── pretty output (degrades to plain when not a TTY) ────────────────────────
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ] && [ "${TERM:-dumb}" != "dumb" ]; then
	BOLD=$'\033[1m'; NC=$'\033[0m'
	BLUE=$'\033[38;5;75m'; GREEN=$'\033[38;5;114m'; RED=$'\033[38;5;203m'; YEL=$'\033[38;5;221m'
else
	BOLD=""; NC=""; BLUE=""; GREEN=""; RED=""; YEL=""
fi
info()  { echo -e "${BLUE}→${NC} $1"; }
ok()    { echo -e "${GREEN}✓${NC} $1"; }
warn()  { echo -e "${YEL}⚠${NC} $1"; }
err()   { echo -e "${RED}✗${NC} $1" >&2; }
pause() { read -rp "Press Enter to close this window… " _ 2>/dev/null || true; }
die()   { err "$1"; pause; exit 1; }

echo -e "${BOLD}${BLUE}◯  steamtools-moon auto-fix${NC}"
echo

command -v curl >/dev/null 2>&1 || die "curl is required but not installed."

# ── 1. resolve the latest -lumen release asset ──────────────────────────────
info "Finding the latest steamtools-moon release"
API="https://api.github.com/repos/${REPO}/releases?per_page=50"
RELEASES_JSON="$(curl -fsSL --connect-timeout 15 --retry 3 --retry-delay 2 \
	-H 'Accept: application/vnd.github+json' "$API")" \
	|| die "Could not reach GitHub. Check your internet connection and try again."

# /releases lists newest first, so the first matching asset is the latest one.
# jq if present (exact), else a grep/sed fallback (no hard jq dependency).
if command -v jq >/dev/null 2>&1; then
	URL="$(printf '%s' "$RELEASES_JSON" | jq -r \
		'[.[] | select(.draft==false) | .assets[]?
		  | select(.name|test("'"$ASSET_RE_JQ"'")) | .browser_download_url] | .[0] // empty')"
else
	URL="$(printf '%s' "$RELEASES_JSON" \
		| grep -oE '"browser_download_url":[[:space:]]*"[^"]*'"$ASSET_RE_GREP"'"' \
		| sed -E 's/.*"(https[^"]+)"$/\1/' | head -n1)"
fi
[ -n "${URL:-}" ] || die "Could not find a steamtools-moon (Lumen) release asset."
ok "Found: $URL"

# ── 2. download + extract ───────────────────────────────────────────────────
TMP="$(mktemp -d)" || die "Could not create a temp directory."
trap 'rm -rf "${TMP:-}"' EXIT
ZIP="$TMP/steamtools-moon.zip"

info "Downloading"
curl -fL --connect-timeout 15 --retry 3 --retry-delay 2 "$URL" -o "$ZIP" \
	|| die "Download failed."

info "Extracting"
if command -v unzip >/dev/null 2>&1; then
	unzip -qo "$ZIP" -d "$TMP/extracted" || die "Extraction failed."
elif command -v python3 >/dev/null 2>&1; then
	python3 - "$ZIP" "$TMP/extracted" <<'PY' || die "Extraction failed."
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], "r") as zf:
    zf.extractall(sys.argv[2])
PY
else
	die "Neither unzip nor python3 is available to extract the archive."
fi

SETUP="$(find "$TMP/extracted" -maxdepth 2 -name setup.sh -type f | head -n1)"
[ -n "$SETUP" ] || die "setup.sh not found in the release archive."
EXTRACT_ROOT="$(dirname "$SETUP")"

# ── 3. install (stops Steam, patches every *steam*.desktop, sudo for system) ─
echo
info "Installing steamtools-moon (this stops Steam and may ask for your password)"
echo
chmod +x "$SETUP" 2>/dev/null || true
( cd "$EXTRACT_ROOT" && bash "$SETUP" install ) || die "steamtools-moon setup failed."
echo
ok "steamtools-moon installed"

# ── 4. relaunch Steam through the injected wrapper ──────────────────────────
STEAM_LAUNCH=""
if [ -x "$WRAPPER" ]; then
	STEAM_LAUNCH="$WRAPPER"
elif command -v steam >/dev/null 2>&1; then
	STEAM_LAUNCH="steam"   # PATH resolves to the wrapper after install
fi

if [ -z "$STEAM_LAUNCH" ]; then
	warn "Could not find the Steam launcher — start Steam yourself to finish."
elif [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
	warn "No graphical session detected — start Steam yourself to finish."
else
	info "Restarting Steam with steamtools-moon active"
	setsid nohup "$STEAM_LAUNCH" >/dev/null 2>&1 < /dev/null &
	ok "Steam is starting. Give it a moment to load."
fi

echo
ok "Done. Your LuaTools games should now show Install/Play instead of Buy."
pause
