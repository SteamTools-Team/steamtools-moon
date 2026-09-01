#!/bin/sh
# Loads SteamTools.so via LD_PRELOAD but in PASSIVE mode: the library is
# mapped (so all its symbols are present in the process) but it installs
# NO hooks and starts NO threads.
#
# If Steam still crashes in passive mode -> the cause is symbol
# interposition / mere presence of the library.
# If Steam runs fine in passive mode -> the cause is our hooking/threading.
SLS_PASSIVE=1 \
LD_PRELOAD="$HOME/.local/share/SteamTools/SteamTools.so${LD_PRELOAD:+:$LD_PRELOAD}" \
	exec /usr/games/steam "$@"
