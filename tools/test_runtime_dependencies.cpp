// TDD regression test for the single runtime-dependency diagnostic.

#include "runtime_dependencies.hpp"

#include <cstdio>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
	int failures = 0;

	void check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::fprintf(stderr, "FAIL: %s\n", message);
			++failures;
		}
	}

	bool createExecutable(const std::string& directory, const char* name,
	                      mode_t mode = 0700)
	{
		const std::string path = directory + "/" + name;
		const int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, mode);
		if (fd < 0) return false;
		close(fd);
		return chmod(path.c_str(), mode) == 0;
	}
}

int main()
{
	check(RuntimeDependencies::missingTools(true, true, true).empty(),
	      "no missing tools produces no detail");
	check(RuntimeDependencies::missingTools(false, true, true) == "unzip",
	      "missing unzip is identified");
	check(RuntimeDependencies::missingTools(true, false, true) == "gzip",
	      "missing gzip is identified");
	check(RuntimeDependencies::missingTools(false, false, true) == "unzip, gzip",
	      "both archive tools share one diagnostic");
	check(RuntimeDependencies::missingTools(true, true, false) == "timeout",
	      "missing timeout is identified");
	check(RuntimeDependencies::missingTools(false, true, false) == "unzip, timeout",
	      "missing unzip and timeout share one diagnostic");
	check(RuntimeDependencies::missingTools(true, false, false) == "gzip, timeout",
	      "missing gzip and timeout share one diagnostic");
	check(RuntimeDependencies::missingTools(false, false, false)
	          == "unzip, gzip, timeout",
	      "all missing tools share one diagnostic");

	char rootTemplate[] = "/tmp/steamtools-runtime-deps-XXXXXX";
	char* rootPath = mkdtemp(rootTemplate);
	if (rootPath == nullptr)
	{
		check(false, "temporary PATH fixture can be created");
	}
	else
	{
		const std::string root(rootPath);
		check(RuntimeDependencies::missingToolsOnPath(root.c_str())
		          == "unzip, gzip, timeout",
		      "PATH probe reports all missing helpers");
		check(createExecutable(root, "unzip"),
		      "PATH fixture creates an executable unzip");
		check(RuntimeDependencies::missingToolsOnPath(root.c_str())
		          == "gzip, timeout",
		      "PATH probe recognizes only executable helpers");
		check(createExecutable(root, "gzip"),
		      "PATH fixture creates an executable gzip");
		check(createExecutable(root, "timeout"),
		      "PATH fixture creates an executable timeout");
		check(RuntimeDependencies::missingToolsOnPath(root.c_str()).empty(),
		      "PATH probe accepts all executable helpers");
		unlink((root + "/unzip").c_str());
		unlink((root + "/gzip").c_str());
		unlink((root + "/timeout").c_str());
		rmdir(root.c_str());
	}

	if (failures != 0)
	{
		std::fprintf(stderr, "test_runtime_dependencies: %d failure(s)\n", failures);
		return 1;
	}
	std::puts("runtime dependency tests passed");
	return 0;
}
