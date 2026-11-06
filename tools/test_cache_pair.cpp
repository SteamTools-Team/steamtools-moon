// Regression tests for the cache-pair publication transaction.
//
// The .bin/.yaml pair and its provenance marker are one publication. Any
// failure must leave the PREVIOUS publication readable, and a success must not
// leave staging copies behind for the next boot to trip over.

#include "../src/feats/cache_pair.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace
{

std::string readAll(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeFixture(const std::string& path, const std::string& contents)
{
	std::string error;
	assert(AtomicFile::write(path, contents, error));
}

// Any staging copy left visible after a completed transaction would outlive
// the publication it describes.
bool backupsPresent(const std::string& bufferPath, const std::string& metaPath)
{
	std::error_code ec;
	return std::filesystem::exists(bufferPath + ".slssteam-pair-backup", ec) ||
	       std::filesystem::exists(metaPath + ".slssteam-pair-backup", ec);
}

} // namespace

int main()
{
	const std::string root =
		"/tmp/slssteam-cache-pair." + std::to_string(getpid());
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);
	assert(!ec);

	const auto binPath = root + "/picsbuffer.bin";
	const auto metaPath = root + "/picsbuffer.yaml";
	std::string error;

	// --- marker publication failure rolls the pair back -------------------
	writeFixture(binPath, "old-buffer");
	writeFixture(metaPath, "old-metadata");

	bool markerPresent = true;
	const bool markerFailurePublished = CachePair::publish(
		binPath, metaPath, "new-buffer", "new-metadata", /*synthetic=*/false,
		/*markerBefore=*/true,
		[&](bool desired) {
			// Removing the stale marker fails; restoring it must succeed.
			if (!desired) return false;
			markerPresent = desired;
			return true;
		},
		[&] { return markerPresent; }, error);

	assert(!markerFailurePublished);
	assert(readAll(binPath) == "old-buffer");
	assert(readAll(metaPath) == "old-metadata");
	assert(markerPresent);
	assert(!backupsPresent(binPath, metaPath));

	// --- a successful publication replaces both members ------------------
	markerPresent = false;
	const bool published = CachePair::publish(
		binPath, metaPath, "fresh-buffer", "fresh-metadata",
		/*synthetic=*/true, /*markerBefore=*/false,
		[&](bool desired) { markerPresent = desired; return true; },
		[&] { return markerPresent; }, error);

	assert(published);
	assert(readAll(binPath) == "fresh-buffer");
	assert(readAll(metaPath) == "fresh-metadata");
	assert(markerPresent);
	assert(!backupsPresent(binPath, metaPath));

	// --- a metadata WRITE failure restores the whole pair ----------------
	// A missing parent directory makes the atomic metadata write fail after
	// the buffer has already been replaced, so the buffer must be undone too.
	const auto unwritableMeta = root + "/absent-dir/picsbuffer.yaml";
	const bool metadataFailurePublished = CachePair::publish(
		binPath, unwritableMeta, "torn-buffer", "torn-metadata",
		/*synthetic=*/true, /*markerBefore=*/true,
		[&](bool desired) { markerPresent = desired; return true; },
		[&] { return markerPresent; }, error);

	assert(!metadataFailurePublished);
	assert(readAll(binPath) == "fresh-buffer");
	assert(!error.empty());
	assert(!backupsPresent(binPath, metaPath));

	// --- an unreadable member aborts before anything is replaced ---------
	// Staging cannot classify a path whose parent is a regular file, so the
	// transaction must refuse it instead of publishing half a pair.
	const auto unstageableMeta = binPath + "/picsbuffer.yaml";
	const bool unstageablePublished = CachePair::publish(
		binPath, unstageableMeta, "ignored-buffer", "ignored-metadata",
		/*synthetic=*/true, /*markerBefore=*/true,
		[&](bool desired) { markerPresent = desired; return true; },
		[&] { return markerPresent; }, error);

	assert(!unstageablePublished);
	assert(readAll(binPath) == "fresh-buffer");
	assert(!backupsPresent(binPath, metaPath));

	// --- a first publication with no previous pair ------------------------
	const auto freshBin = root + "/first.bin";
	const auto freshMeta = root + "/first.yaml";
	markerPresent = false;
	const bool firstPublished = CachePair::publish(
		freshBin, freshMeta, "first-buffer", "first-metadata",
		/*synthetic=*/false, /*markerBefore=*/false,
		[&](bool desired) { markerPresent = desired; return true; },
		[&] { return markerPresent; }, error);

	assert(firstPublished);
	assert(readAll(freshBin) == "first-buffer");
	assert(readAll(freshMeta) == "first-metadata");
	assert(!backupsPresent(freshBin, freshMeta));

	// A failure with no previous pair must not leave a partial one behind.
	const auto blockedFirstMeta = root + "/absent-dir/first.yaml";
	const auto orphanBin = root + "/orphan.bin";
	const bool orphanPublished = CachePair::publish(
		orphanBin, blockedFirstMeta, "orphan-buffer", "orphan-metadata",
		/*synthetic=*/false, /*markerBefore=*/false,
		[&](bool desired) { markerPresent = desired; return true; },
		[&] { return markerPresent; }, error);

	assert(!orphanPublished);
	assert(!std::filesystem::exists(orphanBin, ec));

	std::filesystem::remove_all(root, ec);
	return 0;
}
