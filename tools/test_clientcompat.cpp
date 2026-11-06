// Host regression test for the live IClientCompat bridge.

#include "sdk/IClientCompat.hpp"
#include "feats/compatlive.hpp"
#include "vftableinfo.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
	if (condition) std::printf("ok:   %s\n", message);
	else { std::printf("FAIL: %s\n", message); ++failures; }
}

struct FakeCompat
{
	void** vtable = nullptr;
	std::string appMapping;
	std::string defaultMapping;
	std::uint32_t specifiedAppId = 0;
	std::string specifiedName;
	std::string specifiedConfig;
	int specifiedPriority = 0;
	unsigned int specifyCalls = 0;
	bool reflectSpecified = false;
};

const char* fakeGetCompatToolName(void* self, std::uint32_t appId)
{
	auto& fake = *static_cast<FakeCompat*>(self);
	const std::string& value = appId == 0
		? fake.defaultMapping : fake.appMapping;
	return value.c_str();
}

void fakeSpecifyCompatTool(
	void* self,
	std::uint32_t appId,
	const char* name,
	const char* config,
	int priority)
{
	auto& fake = *static_cast<FakeCompat*>(self);
	fake.specifiedAppId = appId;
	fake.specifiedName = name ? name : "";
	fake.specifiedConfig = config ? config : "";
	fake.specifiedPriority = priority;
	++fake.specifyCalls;
	if (fake.reflectSpecified) fake.appMapping = fake.specifiedName;
}

template <std::size_t N>
std::vector<std::size_t> findAll(
	const std::string& image, const int (&pattern)[N],
	std::size_t begin = 0, std::size_t end = std::string::npos)
{
	std::vector<std::size_t> matches;
	if (end > image.size()) end = image.size();
	for (std::size_t offset = begin; offset + N <= end; ++offset)
	{
		bool equal = true;
		for (std::size_t index = 0; index < N; ++index)
		{
			if (pattern[index] >= 0 &&
				static_cast<unsigned char>(image[offset + index]) !=
					static_cast<unsigned char>(pattern[index]))
			{
				equal = false;
				break;
			}
		}
		if (equal) matches.push_back(offset);
	}
	return matches;
}

std::vector<std::size_t> findCompatOffsetSignature(const std::string& image)
{
	// lea ecx,[esi+disp32] ; pop ; push esi (CUser) ; push ecx (this) ;
	// spill ; call ctor ; pop/pop ; initialize the next CUser member. The
	// displacement is decoded by CompatLive rather than pinned to one build.
	static constexpr int pattern[] = {
		0x8d, 0x8e, -1, -1, -1, -1,
		0x58, 0x56, 0x51,
		0x89, 0x8d, -1, -1, -1, -1,
		0xe8, -1, -1, -1, -1,
		0x58, 0x5a, 0xc7, 0x86, -1, -1, -1, -1,
		0xff, 0xff, 0xff, 0xff,
	};
	return findAll(image, pattern);
}

std::int32_t readI32(const std::string& image, std::size_t offset)
{
	std::int32_t value = 0;
	std::memcpy(&value, image.data() + offset, sizeof(value));
	return value;
}

// PT_LOAD mapping between file offsets and link-time addresses.
struct Load { std::uint32_t offset, vaddr, filesz; };

std::vector<Load> loads(const std::string& image)
{
	std::vector<Load> out;
	std::uint32_t phoff = 0;
	std::uint16_t phnum = 0;
	std::memcpy(&phoff, image.data() + 0x1c, sizeof(phoff));
	std::memcpy(&phnum, image.data() + 0x2c, sizeof(phnum));
	for (std::uint16_t i = 0; i < phnum; ++i)
	{
		const std::size_t ph = phoff + static_cast<std::size_t>(i) * 32;
		std::uint32_t type = 0;
		Load load{};
		std::memcpy(&type, image.data() + ph, 4);
		std::memcpy(&load.offset, image.data() + ph + 4, 4);
		std::memcpy(&load.vaddr, image.data() + ph + 8, 4);
		std::memcpy(&load.filesz, image.data() + ph + 16, 4);
		if (type == 1) out.push_back(load);
	}
	return out;
}

std::optional<std::size_t> toOffset(const std::vector<Load>& map, std::uint32_t va)
{
	for (const auto& load : map)
		if (va >= load.vaddr && va - load.vaddr < load.filesz)
			return load.offset + (va - load.vaddr);
	return std::nullopt;
}

std::optional<std::uint32_t> toVa(const std::vector<Load>& map, std::size_t offset)
{
	for (const auto& load : map)
		if (offset >= load.offset && offset - load.offset < load.filesz)
			return static_cast<std::uint32_t>(load.vaddr + (offset - load.offset));
	return std::nullopt;
}

// Follow the locator's constructor call and decode the vtable it installs:
//   call get_pc_thunk.bx ; add ebx,imm32 ; ... lea eax,[ebx+disp32] ;
//   ... mov [this],eax
std::optional<std::uint32_t> compatVtable(
	const std::string& image, std::size_t locator)
{
	const auto map = loads(image);
	const std::size_t call = locator + 15;
	const std::size_t ctor = call + 5 + readI32(image, call + 1);
	if (ctor + 0x40 > image.size()) return std::nullopt;
	static constexpr int thunkCall[] = {
		0x55, 0x89, 0xe5, 0x57, 0x56, 0x53, 0xe8, -1, -1, -1, -1, 0x81, 0xc3};
	if (findAll(image, thunkCall, ctor, ctor + std::size(thunkCall)).empty())
		return std::nullopt;
	const std::size_t thunk = ctor + 11 + readI32(image, ctor + 7);
	if (thunk + 4 > image.size() ||
		image.compare(thunk, 4, "\x8b\x1c\x24\xc3", 4) != 0)
		return std::nullopt;
	const auto ret = toVa(map, ctor + 11);
	if (!ret) return std::nullopt;
	const std::uint32_t got = *ret + static_cast<std::uint32_t>(readI32(image, ctor + 13));
	static constexpr int leaVtable[] = {0x8d, 0x83, -1, -1, -1, -1};
	const auto lea = findAll(image, leaVtable, ctor + 17, ctor + 0x40);
	if (lea.empty()) return std::nullopt;
	return got + static_cast<std::uint32_t>(readI32(image, lea.front() + 2));
}

// Entry shapes of the two CCompatManager methods SteamTools calls; both are
// unchanged between the cfe99f0c-era and 2026-10 clients.
bool slotHasShape(const std::string& image, std::uint32_t vtable, int index,
	              const int* shape, std::size_t length)
{
	const auto map = loads(image);
	const auto slot = toOffset(map, vtable + static_cast<std::uint32_t>(index) * 4);
	if (!slot) return false;
	const auto entry = toOffset(map, static_cast<std::uint32_t>(readI32(image, *slot)));
	if (!entry || *entry + length > image.size()) return false;
	for (std::size_t i = 0; i < length; ++i)
		if (shape[i] >= 0 &&
			static_cast<unsigned char>(image[*entry + i]) != shape[i])
			return false;
	return true;
}

// SpecifyCompatTool(this, appId, name, config, priority): ebp frame that
// spills all five stack arguments, priority last from [ebp+0x18].
constexpr int kSpecifyShape[] = {
	0x55, 0x89, 0xe5, 0x57, 0xe8, -1, -1, -1, -1, 0x81, 0xc7, -1, -1, -1, -1,
	0x56, 0x53, 0x83, 0xec, 0x2c, 0x8b, 0x45, 0x08, 0x8b, 0x4d, 0x10,
	0x8b, 0x55, 0x14, 0x89, 0x45, 0xe4, 0x8b, 0x45, 0x0c, 0x89, 0x4d, 0xd8,
	0x8b, 0x4d, 0x18,
};
// GetCompatToolName(this, appId): esp frame reading this and appId only,
// then testing the manager's ready flag.
constexpr int kGetNameShape[] = {
	0x55, 0x57, 0x56, 0x53, 0xe8, -1, -1, -1, -1, 0x81, 0xc3, -1, -1, -1, -1,
	0x83, 0xec, 0x1c, 0x8b, 0x7c, 0x24, 0x30, 0x8b, 0x44, 0x24, 0x34,
	0x80, 0xbf,
};
}

int main(int argc, char** argv)
{
	void* vtable[8]{};
	vtable[VFTIndexes::IClientCompat::SpecifyCompatTool] =
		reinterpret_cast<void*>(&fakeSpecifyCompatTool);
	vtable[VFTIndexes::IClientCompat::GetCompatToolName] =
		reinterpret_cast<void*>(&fakeGetCompatToolName);
	FakeCompat fake;
	fake.vtable = vtable;
	auto* compat = reinterpret_cast<IClientCompat*>(&fake);

	compat->specifyCompatTool(582010, "proton-cachyos", "", 250);
	check(fake.specifyCalls == 1 && fake.specifiedAppId == 582010,
	      "the SpecifyCompatTool slot dispatches for the requested app");
	check(fake.specifiedName == "proton-cachyos" &&
	      fake.specifiedConfig.empty() && fake.specifiedPriority == 250,
	      "SpecifyCompatTool preserves name, config, and priority ABI arguments");

	fake.defaultMapping = "proton-cachyos";
	check(std::string(compat->getCompatToolName(0)) == "proton-cachyos",
	      "the GetCompatToolName slot reads the default compatibility mapping");

	fake.appMapping = "GE-Proton";
	const auto existing = CompatLive::step(compat, 582010, false);
	check(existing.status == CompatLive::StepStatus::Ready &&
	      fake.specifyCalls == 1,
	      "an existing per-app mapping is ready without being overwritten");

	fake.appMapping.clear();
	const auto requested = CompatLive::step(compat, 582010, false);
	check(requested.status == CompatLive::StepStatus::Requested &&
	      fake.specifyCalls == 2 && fake.specifiedName == "proton-cachyos",
	      "an absent mapping requests the user's default tool exactly once");
	const auto waiting = CompatLive::step(compat, 582010, true);
	check(waiting.status == CompatLive::StepStatus::Waiting &&
	      fake.specifyCalls == 2,
	      "a submitted asynchronous mapping is polled without resubmission");
	fake.reflectSpecified = true;
	fake.appMapping.clear();
	const auto immediate = CompatLive::step(compat, 582010, false);
	check(immediate.status == CompatLive::StepStatus::Ready &&
	      fake.appMapping == "proton-cachyos",
	      "a synchronously visible mapping completes without another owner frame");
	fake.reflectSpecified = false;
	fake.appMapping.clear();

	fake.defaultMapping.clear();
	const auto fallback = CompatLive::step(compat, 238320, false);
	check(fallback.status == CompatLive::StepStatus::Requested &&
	      fake.specifiedName == "proton_experimental",
	      "Proton Experimental is used only when the default mapping is empty");
	check(CompatLive::step(nullptr, 582010, false).status ==
	      CompatLive::StepStatus::Unavailable,
	      "a missing live interface fails closed");

	const std::uint8_t locator[] = {0x8d, 0x8e, 0xc8, 0x36, 0x00, 0x00};
	const auto decoded = CompatLive::decodeManagerOffset(locator);
	check(decoded.has_value() && *decoded == 0x36c8,
	      "the CUser member locator decodes its little-endian displacement");
	const std::uint8_t invalid[] = {0x8d, 0x86, 0xc8, 0x36, 0x00, 0x00};
	check(!CompatLive::decodeManagerOffset(invalid).has_value(),
	      "a neighboring LEA register form is rejected");
	// The lea ebx form belongs to clients whose IClientCompat vtable still
	// had the three methods removed in 2026-10; the current slot indexes
	// must never be applied to that layout.
	const std::uint8_t retired[] = {0x8d, 0x9e, 0x78, 0x35, 0x00, 0x00};
	check(!CompatLive::decodeManagerOffset(retired).has_value(),
	      "the retired pre-2026-10 locator form is rejected");

	if (argc == 2)
	{
		std::ifstream input(argv[1], std::ios::binary);
		check(static_cast<bool>(input), "Steam client fixture opens");
		if (input)
		{
			const std::string image{
				std::istreambuf_iterator<char>(input),
				std::istreambuf_iterator<char>()};
			const auto matches = findCompatOffsetSignature(image);
			check(matches.size() == 1,
			      "CCompatManager member locator is unique in the Steam client");
			if (matches.size() == 1)
			{
				const auto* bytes = reinterpret_cast<const std::uint8_t*>(
					image.data() + matches.front());
				const auto offset = CompatLive::decodeManagerOffset(
					std::span<const std::uint8_t>(bytes, 6));
				check(offset.has_value() && *offset > 0,
				      "unique live locator yields a positive CUser member offset");

				const auto vtable = compatVtable(image, matches.front());
				check(vtable.has_value(),
				      "the locator's constructor installs a decodable vtable");
				if (vtable)
				{
					check(slotHasShape(image, *vtable,
					          VFTIndexes::IClientCompat::SpecifyCompatTool,
					          kSpecifyShape, std::size(kSpecifyShape)),
					      "the SpecifyCompatTool slot holds the five-argument setter");
					check(slotHasShape(image, *vtable,
					          VFTIndexes::IClientCompat::GetCompatToolName,
					          kGetNameShape, std::size(kGetNameShape)),
					      "the GetCompatToolName slot holds the per-app name lookup");
				}
			}
		}
	}

	if (failures == 0)
	{
		std::printf("client compat tests passed\n");
		return 0;
	}
	std::printf("%d client compat check(s) failed\n", failures);
	return 1;
}
