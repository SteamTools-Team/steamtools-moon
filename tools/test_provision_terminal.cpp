// SPDX-License-Identifier: AGPL-3.0-only

#include "../src/feats/provision_terminal.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, msg)                                                     \
	do {                                                                     \
		if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_failures; }       \
		else         { std::printf("ok:   %s\n", msg); }                     \
	} while (0)

int main()
{
	using namespace ProvisionTerminal;
	namespace fs = std::filesystem;

	Record parsed;
	const std::string canonical =
		"schema: 1\n"
		"appid: 4496490\n"
		"kind: virtual_dlc\n"
		"change_number: 37981055\n"
		"input_fingerprint: -\n";
	CHECK(parse(canonical, parsed) && render(parsed) == canonical,
	      "terminal record round-trips canonically");
	CHECK(applies(parsed, 4496490, 37981055, "ignored"),
	      "matching virtual DLC verdict applies");
	CHECK(!applies(parsed, 4496490, 37981056, "ignored"),
	      "newer product info invalidates a virtual DLC verdict");

	const Record noContent{
		1, 420530, Kind::NoUsableContent, 12, "0123456789abcdef"};
	CHECK(applies(noContent, 420530, 12, "0123456789abcdef"),
	      "no-content verdict applies to matching local inputs");
	CHECK(!applies(noContent, 420530, 12, "fedcba9876543210"),
	      "local input changes invalidate no-content verdicts");
	CHECK(!parse("schema: 1\nappid: 0\nkind: virtual_dlc\n"
	             "change_number: 1\ninput_fingerprint: -\n", parsed),
	      "zero appid is rejected");
	CHECK(!parse("schema: 1\nappid: 1\nkind: network_failure\n"
	             "change_number: 1\ninput_fingerprint: -\n", parsed),
	      "transient failures cannot be persisted as terminal");
	CHECK(!parse("schema: 1\nappid: 1\nkind: virtual_dlc\n"
	             "change_number: 0\ninput_fingerprint: -\n", parsed),
	      "zero change numbers are rejected");
	CHECK(!parse("schema: 1\nappid: 1\nkind: no_usable_content\n"
	             "change_number: 1\ninput_fingerprint: ABCDEF0123456789\n", parsed),
	      "noncanonical fingerprints are rejected");
	CHECK(!parse("schema: 1\r\nappid: 1\nkind: virtual_dlc\n"
	             "change_number: 1\ninput_fingerprint: -\n", parsed),
	      "CR-containing records are rejected");
	CHECK(!parse("schema: 01\nappid: 1\nkind: virtual_dlc\n"
	             "change_number: 1\ninput_fingerprint: -\n", parsed),
	      "noncanonical numeric fields are rejected");
	CHECK(!parse("schema: 1\nappid: 1\nkind: virtual_dlc\n"
	             "change_number: 1\ninput_fingerprint: -\nextra: x\n", parsed),
	      "unknown trailing fields are rejected");

	const std::vector<LocalInput> inputs{
		{200, "key-b", 1000},
		{100, "key-a", 900},
	};
	const std::vector<LocalInput> reordered{
		{100, "key-a", 900},
		{200, "key-b", 1000},
	};
	CHECK(fingerprint(inputs) == fingerprint(reordered),
	      "local input fingerprint is order independent");
	CHECK(fingerprint(inputs) !=
	          fingerprint({{100, "key-a", 901}, {200, "key-b", 1000}}),
	      "manifest gid changes alter the local input fingerprint");
	CHECK(fingerprint(inputs) !=
	          fingerprint({{100, "key-c", 900}, {200, "key-b", 1000}}),
	      "key replacement alters the local input fingerprint");

	const fs::path tempDir = fs::temp_directory_path() /
		("steamtools_provision_terminal_" + std::to_string(::getpid()));
	std::error_code ec;
	fs::remove_all(tempDir, ec);
	fs::create_directories(tempDir, ec);
	Store store(tempDir.string());
	CHECK(store.publish(noContent), "terminal state publishes atomically");
	const auto loaded = store.load(420530);
	CHECK(loaded.status == LoadStatus::Valid && loaded.record == noContent,
	      "published terminal state survives a new read");
	CHECK(store.erase(420530) && store.load(420530).status == LoadStatus::Missing,
	      "terminal state can be invalidated explicitly");
	{
		std::ofstream invalid(store.path(420530), std::ios::binary);
		invalid << "schema: 1\nappid: 420531\nkind: virtual_dlc\n"
		           "change_number: 1\ninput_fingerprint: -\n";
	}
	CHECK(store.load(420530).status == LoadStatus::Invalid,
	      "state files cannot be reused for a different app");
	{
		std::ofstream oversized(store.path(420530), std::ios::binary);
		oversized << canonical;
		oversized << std::string(4096, 'x');
	}
	CHECK(store.load(420530).status == LoadStatus::Invalid,
	      "terminal state rejects data beyond the bounded read limit");

	fs::remove_all(tempDir, ec);
	if (g_failures == 0) std::printf("\nAll provision terminal tests passed.\n");
	else                 std::printf("\n%d provision terminal test(s) FAILED.\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
