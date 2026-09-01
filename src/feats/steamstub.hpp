// SPDX-License-Identifier: AGPL-3.0-only
//
// Wrapper integration: invokes a bundled helper script that
// processes Windows binaries belonging to AdditionalApps before
// Steam launches them through Proton. Drives Steamless under a
// dedicated Wine prefix; runs synchronously inside the
// IClientAppManager::LaunchApp hook.
//
// Layout expected at runtime:
//   <install-root>/tools/steamstub-bypass/run-steamless.sh
//   <install-root>/tools/steamless-bin/Steamless.CLI.exe
//   <install-root>/tools/steamless-bin/Plugins/*.dll
//
// The .so also probes ~/.local/share/SteamTools/{steamstub-bypass,
// steamless-bin}/ as a user-local fallback for system-package
// installs where the install root is read-only.

#pragma once

#include <cstdint>

namespace SteamStub
{
	// Resolve helper paths from `installRoot` (the directory
	// holding SteamTools.so).  Idempotent.  Logs a debug note and
	// disables the feature if the helper script or third-party
	// binaries are missing.
	void setup(const char* installRoot);

	// Kick off a one-shot background prewarm of the dedicated
	// Wine prefix so the first onLaunchApp call doesn't pay the
	// initial wineboot cost.  Safe to call multiple times; only
	// the first call spawns a worker.  No-op when the feature is
	// disabled.  onLaunchApp blocks on its completion just before
	// invoking the helper.
	void warmupAsync();

	// Called from the LaunchApp hook for every app launch.  No-ops
	// for apps not listed in AdditionalApps.  For configured apps
	// scans the install dir for .exe files, runs the helper script
	// on each one that needs processing, blocks until the helper
	// exits.  Re-entry safe via on-disk marker files.
	void onLaunchApp(uint32_t appId);
}
