// Regression test for atomic cache/appinfo replacement.

#include "../src/utils/atomic_file.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

int main()
{
	const std::string path =
		"/tmp/slssteam-test-atomic-file." + std::to_string(getpid());
	std::error_code ec;
	std::filesystem::remove(path, ec);

	std::string error;
	assert(AtomicFile::write(path, "first", error));
	assert(error.empty());
	assert(AtomicFile::write(path, "second", error));

	AtomicFile::FileIdentity identity{};
	assert(AtomicFile::readIdentity(path, identity));
	assert(AtomicFile::writeIfUnchanged(path, identity, "third", error));

	AtomicFile::FileIdentity staleIdentity{};
	assert(AtomicFile::readIdentity(path, staleIdentity));
	assert(AtomicFile::write(path, "external", error));
	assert(!AtomicFile::writeIfUnchanged(path, staleIdentity, "plugin", error));

	std::ifstream in(path, std::ios::binary);
	std::string contents((std::istreambuf_iterator<char>(in)),
	                     std::istreambuf_iterator<char>());
	assert(contents == "external");

#ifdef ATOMIC_FILE_TESTING
	AtomicFile::setBeforeConditionalPublishHook([&] {
		std::string hookError;
		assert(AtomicFile::write(path, "external-window", hookError));
		AtomicFile::setBeforeConditionalPublishHook(nullptr);
	});

	AtomicFile::FileIdentity windowIdentity{};
	assert(AtomicFile::readIdentity(path, windowIdentity));
	assert(!AtomicFile::writeIfUnchanged(
		path, windowIdentity, "must-not-win-the-window", error));
	assert(error == "file changed during conditional publish" ||
	       error == "conditional publish rolled back after identity change");

	std::ifstream windowIn(path, std::ios::binary);
	std::string windowContents(
		(std::istreambuf_iterator<char>(windowIn)),
		std::istreambuf_iterator<char>());
	assert(windowContents == "external-window");

	AtomicFile::setBeforeConditionalRollbackHook([&] {
		std::string hookError;
		assert(AtomicFile::write(path, "external-after-exchange", hookError));
	});

	AtomicFile::FileIdentity rollbackIdentity{};
	assert(AtomicFile::readIdentity(path, rollbackIdentity));
	assert(!AtomicFile::writeIfUnchanged(
		path, rollbackIdentity, "must-not-delete-external", error));
	assert(error.find("file changed during conditional publish") == 0);

	std::ifstream rollbackIn(path, std::ios::binary);
	std::string rollbackContents(
		(std::istreambuf_iterator<char>(rollbackIn)),
		std::istreambuf_iterator<char>());
	assert(rollbackContents == "external-after-exchange");
#endif

	bool conflictPreserved = false;
	for (const auto& entry : std::filesystem::directory_iterator(
			std::filesystem::path(path).parent_path()))
	{
		const auto name = entry.path().filename().string();
		if (name.find(".slssteam-conditional-conflict.") != std::string::npos)
		{
			conflictPreserved = true;
			std::filesystem::remove(entry.path(), ec);
			continue;
		}
		assert(name.find("slssteam-test-atomic-file.") == std::string::npos
		       || entry.path() == path);
	}
	assert(conflictPreserved);

	std::filesystem::remove(path, ec);
	return 0;
}
