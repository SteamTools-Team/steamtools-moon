// SPDX-License-Identifier: AGPL-3.0-only
//
// Two-member cache publication (.bin + .yaml) plus its provenance marker.
//
// The pair and the marker are ONE logical publication: a reader that sees a
// synthetic buffer without its marker, or a normal buffer still carrying an
// old marker, cannot tell which half is authoritative.  This helper makes the
// three writes behave as a single transaction for every in-process failure and
// keeps the previous publication readable when the process dies mid-write.
//
// Staging uses copy, not rename: renaming both members aside first would leave
// a window where NEITHER is on disk, so an interrupted publication would erase
// a usable pair instead of degrading to the pre-existing "new buffer + old
// metadata" case, which readers already reject via wire_size/SHA-1 validation.
// Backup names are fixed rather than unique because every caller holds both
// the in-process publication mutex and the cross-process cache lock, so two
// publications for the same app can never stage concurrently — and a crash
// therefore leaves at most one stale copy per member instead of an unbounded
// pile of them.

#pragma once

#include "provision_cache.hpp"

#include "../utils/atomic_file.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace CachePair
{

namespace detail
{

inline std::string backupPath(const std::string& path)
{
	return path + ".slssteam-pair-backup";
}

// Copy the current member aside so a later failure can restore it byte for
// byte.  `staged` stays false when there is nothing to preserve.
inline bool stageMember(const std::string& path, bool& staged,
                        std::string& error)
{
	staged = false;
	std::error_code ec;
	const bool exists = std::filesystem::exists(path, ec);
	if (ec)
	{
		error = "cannot inspect cache pair member " + path + ": " +
		         ec.message();
		return false;
	}
	if (!exists)
	{
		// A previous crash may have left a stale copy; it no longer describes
		// any visible member, so drop it instead of restoring it later.
		std::filesystem::remove(backupPath(path), ec);
		return true;
	}

	std::filesystem::copy_file(path, backupPath(path),
		std::filesystem::copy_options::overwrite_existing, ec);
	if (ec)
	{
		error = "cannot stage cache pair member " + path + ": " +
		         ec.message();
		return false;
	}
	staged = true;
	return true;
}

// Restore the exact previous member. Returns false when the old state could
// not be reinstated, which the caller reports so the failure is not mistaken
// for a clean rejection.
inline bool restoreMember(const std::string& path, bool staged,
                          std::string& error)
{
	std::error_code ec;
	if (!staged)
	{
		// The member did not exist before, so removing the new one restores
		// the previous state exactly.
		if (std::filesystem::exists(path, ec) &&
		    !std::filesystem::remove(path, ec))
		{
			error += "; unable to remove failed cache member " + path;
			if (ec) error += ": " + ec.message();
			return false;
		}
		return !ec;
	}

	std::filesystem::rename(backupPath(path), path, ec);
	if (ec)
	{
		error += "; unable to restore cache member " + path + ": " +
		         ec.message();
		return false;
	}
	return true;
}

inline void discardBackup(const std::string& path, bool staged)
{
	if (!staged) return;
	std::error_code ec;
	std::filesystem::remove(backupPath(path), ec);
}

} // namespace detail

// Publish a complete cache pair while the caller holds the process and
// cross-process cache locks. `setMarker(desired)` creates or removes the
// provenance marker and `markerIsPresent()` reports its state; both are
// supplied by the owning module so this helper stays free of cache-layout
// knowledge and is deterministic in the standalone regression test.
//
// Returns false for every failure, leaving the previous pair and marker in
// place. `error` describes the failure and additionally reports when the
// previous state could NOT be restored, which needs operator attention rather
// than a silent retry.
inline bool publish(
	const std::string& bufferPath, const std::string& metadataPath,
	const std::string& buffer, const std::string& metadata, bool synthetic,
	bool markerBefore, const std::function<bool(bool)>& setMarker,
	const std::function<bool()>& markerIsPresent, std::string& error)
{
	error.clear();
	bool bufferStaged = false;
	bool metadataStaged = false;

	if (!detail::stageMember(bufferPath, bufferStaged, error)) return false;
	if (!detail::stageMember(metadataPath, metadataStaged, error))
	{
		(void)detail::restoreMember(bufferPath, bufferStaged, error);
		return false;
	}

	const auto rollback = [&] {
		const bool bufferRestored =
			detail::restoreMember(bufferPath, bufferStaged, error);
		const bool metadataRestored =
			detail::restoreMember(metadataPath, metadataStaged, error);
		if (!bufferRestored || !metadataRestored)
			error += "; previous cache pair could not be restored";
	};

	if (!AtomicFile::write(bufferPath, buffer, error))
	{
		rollback();
		return false;
	}
	if (!AtomicFile::write(metadataPath, metadata, error))
	{
		rollback();
		return false;
	}

	const bool markerOperationSucceeded = setMarker(synthetic);
	if (!AppInfoProvision::cache::syntheticMarkerPublicationConsistent(
	        synthetic, markerOperationSucceeded, markerIsPresent()))
	{
		error = markerOperationSucceeded
		    ? "cache marker state is inconsistent"
		    : "cache marker publication failed";
		if (!setMarker(markerBefore))
			error += "; cache marker rollback failed";
		rollback();
		return false;
	}

	detail::discardBackup(bufferPath, bufferStaged);
	detail::discardBackup(metadataPath, metadataStaged);
	return true;
}

} // namespace CachePair
