// Validate the optional runtime-capability locators against the installed
// 32-bit Steam client.
//
// These are the compiled locators whose failure silently degrades a
// feature instead of failing the load, so a signature that stops matching after
// a client update is easy to miss:
//
//   CUser::ProcessPendingLicenseUpdates  PackagePatch loses the runtime license
//                                        refresh, so adding a game needs a
//                                        Steam restart again.
//   CCMInterface::RecvPkt                Family Share message filtering turns
//                                        off.
//   CSteamApp::OwnershipFlagsReference    LibraryRemoval cannot derive the
//                                        ownership-flags field offset, so
//                                        visual library removal turns off.
//   ParentalSignatureCheck               the local parental unlock turns off.
//   CAPIJob::GetPlayerStats              the stats-job debug trace turns off.
//   CUser::m_CompatManager               live compatibility-tool mapping for
//                                        newly added titles turns off.
//   CDepotDownloadMgr::BuildDepotDependency
//                                        install-plan pinning, empty-depot
//                                        pruning and event-driven manifest
//                                        staging turn off.
//
// Each signature is asserted to be present verbatim in src/patterns.cpp, so
// this test can never pass against a stale private copy of the pattern, and to
// resolve to exactly one match in the module it belongs to.
//
// Build and run with:
//   make test-optional-locators
// or point it at another install:
//   make test-optional-locators STEAMCLIENT=... STEAMUI=...

#include "feats/compatlive.hpp"
#include "feats/libraryremoval_policy.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int failures = 0;

void expect(bool condition, std::string_view message)
{
	if (condition)
		return;
	std::cerr << "FAIL: " << message << '\n';
	++failures;
}

struct Byte
{
	std::uint8_t value;
	bool wildcard;
};

std::vector<Byte> parse(std::string_view pattern)
{
	std::vector<Byte> bytes;
	std::size_t cursor = 0;
	while (cursor < pattern.size())
	{
		while (cursor < pattern.size() && pattern[cursor] == ' ')
			++cursor;
		if (cursor >= pattern.size())
			break;
		const std::size_t end = pattern.find(' ', cursor);
		const std::string token(pattern.substr(
			cursor, end == std::string_view::npos ? end : end - cursor));
		bytes.push_back({
			static_cast<std::uint8_t>(
				token == "?" ? 0 : std::stoul(token, nullptr, 16)),
			token == "?",
		});
		cursor = end == std::string_view::npos ? pattern.size() : end;
	}
	return bytes;
}

std::vector<std::size_t> findMatches(
	const std::string& image, const std::vector<Byte>& pattern)
{
	std::vector<std::size_t> matches;
	if (pattern.empty() || image.size() < pattern.size())
		return matches;
	for (std::size_t offset = 0; offset + pattern.size() <= image.size(); ++offset)
	{
		bool match = true;
		for (std::size_t index = 0; index < pattern.size(); ++index)
		{
			if (!pattern[index].wildcard
			    && static_cast<std::uint8_t>(image[offset + index])
			           != pattern[index].value)
			{
				match = false;
				break;
			}
		}
		if (match)
			matches.push_back(offset);
	}
	return matches;
}

std::string readFile(const char* path)
{
	std::ifstream input(path, std::ios::binary);
	if (!input)
	{
		std::cerr << "could not open " << path << '\n';
		std::exit(2);
	}
	return std::string{std::istreambuf_iterator<char>(input),
	                   std::istreambuf_iterator<char>()};
}

// Returns the single match offset, or npos after reporting the failure.
std::size_t resolveUnique(
	const std::string& image,
	std::string_view name,
	std::string_view signature,
	const std::string& source)
{
	expect(source.find(signature) != std::string::npos,
	       std::string(name) + ": signature is not the one compiled in "
	                           "src/patterns.cpp");
	const auto matches = findMatches(image, parse(signature));
	if (matches.size() == 1)
		return matches.front();
	std::cerr << "FAIL: " << name << ": expected exactly one match, found "
	          << matches.size() << '\n';
	++failures;
	return std::string::npos;
}
} // namespace

int main(int argc, char** argv)
{
	if (argc < 3 || argc > 4)
	{
		std::cerr << "usage: test_optional_locators <steamclient.so> "
		             "<steamui.so> [src/patterns.cpp]\n";
		return 2;
	}
	const std::string client = readFile(argv[1]);
	const std::string ui = readFile(argv[2]);
	const std::string source = readFile(argc == 4 ? argv[3] : "src/patterns.cpp");

	// The trailing `lea eax,[ebx+disp32]` reaches a data symbol through the PIC
	// base, so its displacement moves whenever the GOT layout shifts. The hook
	// only calls the resolved entry, so it stays wildcarded. The two CUser
	// member offsets it loads (count near 0x1C7C, array base near 0x1C70) drift
	// by 4 across builds (0x1C7C->0x1C78, 0x1C70->0x1C6C on a later beta); they
	// are internal to the function, so only their low byte is masked while the
	// 0x1C high byte stays pinned.
	(void)resolveUnique(
		client, "CUser::ProcessPendingLicenseUpdates",
		"55 57 56 53 E8 ? ? ? ? 81 C3 ? ? ? ? 83 EC 2C 8B 44 24 40 8B 88 ? 1C "
		"00 00 85 C9 0F 8E ? ? ? ? 05 ? 1C 00 00 89 44 24 18 8D 83 ? ? ? ? 8B 30",
		source);

	// The stack frame grew on a later beta (sub esp,0x4cc -> larger), drifting
	// both the frame immediate and the frame-relative spill of the saved
	// argument together. Both are local-frame layout, not values the hook reads,
	// so mask the frame immediate and the spill displacement (high bytes kept).
	(void)resolveUnique(
		client, "CCMInterface::RecvPkt",
		"55 89 E5 57 56 E8 ? ? ? ? 81 C6 ? ? ? ? 53 81 EC ? ? 00 00 8B 45 08 "
		"8B 7D 0C 89 85 ? ? FF FF 65 A1 14 00 00 00 89 45 E4 31 C0 8B 86",
		source);

	// FillInAppOverview serializes several fields with this very shape, one
	// after another, so the discriminator has to be the field offset. The
	// ownership read stays at 0x18 on both the 2026-08-03 and the 2026-08-16
	// client, while the block immediately after it reads 0x50. What moved is
	// only the register holding the app pointer (ecx -> edx), so the modrm byte
	// is the wildcard and the displacement stays pinned. Wildcarding the
	// displacement instead also matches exactly once on the newer client, but
	// it lands on that neighbouring field, and LibraryRemoval would then write
	// hidden ownership bits into it.
	const std::size_t ownership = resolveUnique(
		ui, "CSteamApp::OwnershipFlagsReference",
		"8B ? 18 8B 9D ? ? ? ? 83 EC 04 50 8D 83 ? ? ? ? 50 FF B5 ? ? ? ? "
		"E8 ? ? ? ?",
		source);
	if (ownership != std::string::npos)
	{
		const auto* bytes =
			reinterpret_cast<const std::uint8_t*>(ui.data()) + ownership;
		const auto offset = LibraryRemovalPolicy::deriveOwnershipOffset(
			std::span<const std::uint8_t>(bytes, 3));
		expect(offset.has_value(),
		       "CSteamApp::OwnershipFlagsReference: the matched instruction does "
		       "not decode to a usable ownership-flags offset");
		if (offset)
		{
			std::cout << "CSteamApp::OwnershipFlags offset = 0x" << std::hex
			          << *offset << std::dec << '\n';
		}
	}

	// The failure path pushes a member near 0x182 (0x166 on the 2026-10
	// client) for its log line; only the branch at +2 is patched, so the
	// member offset is masked. The jne distance (0x27) keeps the sibling
	// check (jne 0x63) out.
	const std::size_t parental = resolveUnique(
		client, "ParentalSignatureCheck",
		"84 C0 75 27 8B 85 ? ? ? ? 8D 9D ? ? ? ? 83 EC 04 FF B0 ? ? 00 00 "
		"8D 86 ? ? ? ? 50 53 E8 ? ? ? ?",
		source);
	if (parental != std::string::npos)
	{
		expect(static_cast<std::uint8_t>(client[parental + 2]) == 0x75,
		       "ParentalSignatureCheck: the patched byte is not the jne");
	}

	// Direct entry of the CAPIJobRequestUserStats job body (the function
	// carrying "Player.GetUserStats#1"); its only call site was rewritten on
	// the 2026-10 client, so the call-site form no longer exists.
	(void)resolveUnique(
		client, "CAPIJob::GetPlayerStats",
		"55 89 E5 57 E8 ? ? ? ? 81 C7 ? ? ? ? 56 53 81 EC ? ? 00 00 8B 75 08 "
		"89 B5 ? ? FF FF 89 FB 65 A1 14 00 00 00 89 45 E4 31 C0 8D 85 ? ? FF "
		"FF 6A 00 50",
		source);

	// The CUser constructor's embedded CCompatManager; the matched LEA must
	// decode to the member offset CSteamEngine uses at runtime.
	const std::size_t compat = resolveUnique(
		client, "CUser::m_CompatManager",
		"8D 8E ? ? ? ? 58 56 51 89 8D ? ? ? ? E8 ? ? ? ? 58 5A C7 86 ? ? ? ? "
		"FF FF FF FF",
		source);
	if (compat != std::string::npos)
	{
		const auto* bytes =
			reinterpret_cast<const std::uint8_t*>(client.data()) + compat;
		expect(CompatLive::decodeManagerOffset(
			       std::span<const std::uint8_t>(bytes, 6)).has_value(),
		       "CUser::m_CompatManager: the matched LEA does not decode");
	}

	// The install-plan builder (eight flag-0/flag-1 callers). The 2026-10
	// client shrank its frame to 0x43c and reassigned its argument registers;
	// the yielding sibling that kept the old 0x48c frame is not this hook.
	(void)resolveUnique(
		client, "CDepotDownloadMgr::BuildDepotDependency",
		"E8 ? ? ? ? 05 ? ? ? ? 55 89 E5 57 56 53 81 EC 3C 04 00 00 8B 5D 08 "
		"8B 75 14 89 85 ? ? FF FF 8B 45 0C",
		source);

	if (failures != 0)
	{
		std::cerr << failures << " optional locator check(s) failed\n";
		return 1;
	}
	std::cout << "optional runtime-capability locators resolve uniquely\n";
	return 0;
}
