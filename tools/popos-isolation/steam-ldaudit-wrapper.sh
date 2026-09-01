#!/bin/sh
# SteamTools wrapper - injects via LD_AUDIT (rtld-audit).
SLSDIR="$HOME/.local/share/SteamTools"
LD_AUDIT="$SLSDIR/library-inject.so:$SLSDIR/SteamTools.so${LD_AUDIT:+:$LD_AUDIT}" exec /usr/games/steam "$@"
