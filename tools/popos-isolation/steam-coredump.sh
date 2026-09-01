#!/bin/sh
# Launches Steam under SteamTools (LD_AUDIT) with core dumps enabled so we can
# get a precise backtrace of the SIGILL in the IPC:CSteamEngine thread.
ulimit -c unlimited
SLSDIR="$HOME/.local/share/SteamTools"
LD_AUDIT="$SLSDIR/library-inject.so:$SLSDIR/SteamTools.so${LD_AUDIT:+:$LD_AUDIT}" exec /usr/games/steam "$@"
