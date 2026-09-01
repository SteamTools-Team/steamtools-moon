#!/bin/bash
# Isolation harness for the Pop!_OS crash investigation.
#
# Toggles SteamTools (LD_PRELOAD wrapper) and Millennium (libXtst.so.6
# bootstrap symlink) independently so we can see which component — or
# their combination — triggers the SIGABRT at store-load on Pop!_OS.
#
# Usage:
#   ./test-isolation.sh sls-only      # SteamTools, no Millennium
#   ./test-isolation.sh mln-only      # Millennium, no SteamTools
#   ./test-isolation.sh neither       # vanilla Steam
#   ./test-isolation.sh both          # restore normal (SteamTools + Millennium)
#   ./test-isolation.sh status        # show current state
#
# It only flips the Millennium libXtst symlink and chooses the launch
# command. It never deletes anything. Run Steam manually afterwards with
# the printed command; quit Steam fully before switching modes.

set -u

STEAM64="$HOME/.steam/debian-installation/ubuntu12_64"
XTST_LINK="$STEAM64/libXtst.so.6"
MLN_BOOT="/usr/lib/millennium/libmillennium_bootstrap_hhx64.so"
SYS_XTST="/usr/lib/x86_64-linux-gnu/libXtst.so.6"
SLS_WRAPPER="$HOME/.local/share/SteamTools/path/steam"
SAVED_LINK="$STEAM64/.libXtst.so.6.millennium"   # remembers Millennium target

millennium_on() {
	ln -sf "$MLN_BOOT" "$XTST_LINK"
	echo "Millennium: ENABLED (libXtst.so.6 -> millennium bootstrap)"
}

millennium_off() {
	ln -sf "$SYS_XTST" "$XTST_LINK"
	echo "Millennium: DISABLED (libXtst.so.6 -> system libXtst)"
}

show_status() {
	echo "libXtst.so.6 -> $(readlink "$XTST_LINK")"
	echo "SteamTools wrapper: $SLS_WRAPPER"
}

case "${1:-}" in
	sls-only)
		millennium_off
		echo "RUN:  $SLS_WRAPPER"
		;;
	mln-only)
		millennium_on
		echo "RUN:  /usr/games/steam"
		;;
	neither)
		millennium_off
		echo "RUN:  /usr/games/steam"
		;;
	both)
		millennium_on
		echo "RUN:  $SLS_WRAPPER"
		;;
	status)
		show_status
		;;
	*)
		echo "Usage: $0 {sls-only|mln-only|neither|both|status}"
		exit 1
		;;
esac
