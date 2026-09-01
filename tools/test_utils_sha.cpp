// TDD regression test for best-effort file hashing.

#include "utils.hpp"

#include <cstdio>
#include <fstream>
#include <string>

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
}

int main()
{
	const std::string missing = "/tmp/steamtools-test-utils-sha-missing";
	std::remove(missing.c_str());
	check(Utils::getFileSHA256(missing.c_str()).empty(),
	      "missing file returns an empty digest");

	const std::string path = "/tmp/steamtools-test-utils-sha-input";
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << "abc";
	}
	check(Utils::getFileSHA256(path.c_str()) ==
	          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
	      "readable file keeps SHA-256 behavior");
	std::remove(path.c_str());

	if (failures != 0)
	{
		std::fprintf(stderr, "test_utils_sha: %d failure(s)\n", failures);
		return 1;
	}
	std::puts("utils SHA tests passed");
	return 0;
}
