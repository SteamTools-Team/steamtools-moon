// Runtime ABI smoke test for the vendored yaml-cpp headers/static archive.
//
// A static archive built with a newer libstdc++ implementation can link in
// the Ubuntu 22.04 portable builder yet corrupt yaml-cpp's internal STL state
// at runtime. Exercise both emission and parsing so the builder rejects that
// false-positive link before an SteamTools artifact is deployed.

#include "yaml-cpp/yaml.h"

#include <cstdio>
#include <string>

int main()
{
	YAML::Emitter emitter;
	emitter << YAML::BeginMap
	        << YAML::Key << "appid" << YAML::Value << 42u
	        << YAML::Key << "normalized" << YAML::Value << false
	        << YAML::EndMap;
	if (!emitter.good())
	{
		std::fprintf(stderr, "yaml emitter failed: %s\n",
		             emitter.GetLastError().c_str());
		return 1;
	}

	const YAML::Node parsed = YAML::Load(emitter.c_str());
	if (!parsed.IsMap() || parsed["appid"].as<unsigned int>() != 42u ||
	    parsed["normalized"].as<bool>())
	{
		std::fputs("yaml round-trip mismatch\n", stderr);
		return 1;
	}

	std::puts("yaml runtime ABI smoke test passed");
	return 0;
}
