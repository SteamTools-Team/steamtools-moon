// Standalone test for the TLS CA-bundle discovery (cainfo.hpp).
//
// Every libcurl handle in the project (CM list fetch, native-CM TLS socket,
// steamcmd fallback, ManifestFetch, update check) shares one rule for where
// to find the system trust store, so a distro that doesn't match libcurl's
// compiled-in default (SteamOS/Arch: the real bundle lives under
// /etc/ca-certificates/extracted, only symlinked into /etc/ssl) still
// verifies peers instead of failing with "Peer certificate cannot be
// authenticated with given CA certificates".
//
// The discovery is PURE (env-getter + path-exists predicate injected) so it
// is unit-testable without touching the real filesystem.
//
// Build (from repo root):
//   g++ -std=c++20 -I include tools/test_cainfo.cpp -o /tmp/test_cainfo && /tmp/test_cainfo

#include "../src/cainfo.hpp"

#include <cstdio>
#include <map>
#include <set>
#include <string>

static int g_failures = 0;

#define CHECK(cond, msg)                                                     \
	do {                                                                     \
		if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_failures; }       \
		else         { std::printf("ok:   %s\n", msg); }                     \
	} while (0)

// Build an env-getter from a map.  Missing key -> nullptr.  The map is owned
// by the returned closure (captured by value) so the backing strings outlive
// the temporary passed at the call site.
static std::function<const char*(const char*)> envFrom(std::map<std::string, std::string> m)
{
	return [m = std::move(m)](const char* key) -> const char* {
		auto it = m.find(key ? key : "");
		return it == m.end() ? nullptr : it->second.c_str();
	};
}

// Build an exists-predicate from a set of "present" paths (owned by value).
static std::function<bool(const std::string&)> existsFrom(std::set<std::string> present)
{
	return [present = std::move(present)](const std::string& p) { return present.count(p) != 0; };
}

int main()
{
	// 1) CURL_CA_BUNDLE set and present -> used verbatim, no probing.
	{
		auto env = envFrom({{"CURL_CA_BUNDLE", "/custom/ca.pem"}});
		auto ex  = existsFrom({"/custom/ca.pem", "/etc/ssl/certs/ca-certificates.crt"});
		const auto r = ca::discoverWith(env, ex);
		CHECK(r.file == "/custom/ca.pem", "CURL_CA_BUNDLE present -> used verbatim");
	}

	// 2) SSL_CERT_FILE honored when CURL_CA_BUNDLE absent.
	{
		auto env = envFrom({{"SSL_CERT_FILE", "/x/sslcert.pem"}});
		auto ex  = existsFrom({"/x/sslcert.pem"});
		const auto r = ca::discoverWith(env, ex);
		CHECK(r.file == "/x/sslcert.pem", "SSL_CERT_FILE honored");
	}

	// 3) SLSSTEAM_CA_BUNDLE override wins over the standard env vars.
	{
		auto env = envFrom({{"SLSSTEAM_CA_BUNDLE", "/override.pem"},
		                    {"CURL_CA_BUNDLE",     "/curl.pem"}});
		auto ex  = existsFrom({"/override.pem", "/curl.pem"});
		const auto r = ca::discoverWith(env, ex);
		CHECK(r.file == "/override.pem", "SLSSTEAM_CA_BUNDLE overrides CURL_CA_BUNDLE");
	}

	// 4) An env path that does NOT exist is skipped; discovery falls through
	//    to the next present env var, then to the probe list.
	{
		auto env = envFrom({{"CURL_CA_BUNDLE", "/missing.pem"},
		                    {"SSL_CERT_FILE",  "/present.pem"}});
		auto ex  = existsFrom({"/present.pem"});
		const auto r = ca::discoverWith(env, ex);
		CHECK(r.file == "/present.pem", "missing env path skipped, next present env used");
	}

	// 5) Empty env values are treated as unset (no false positive).
	{
		auto env = envFrom({{"CURL_CA_BUNDLE", ""}});
		auto ex  = existsFrom({"/etc/ssl/certs/ca-certificates.crt"});
		const auto r = ca::discoverWith(env, ex);
		CHECK(r.file == "/etc/ssl/certs/ca-certificates.crt",
		      "empty env value ignored, probe list used");
	}

	// 6) No env at all: probe list picks the first present candidate.
	{
		auto env = envFrom({});
		auto ex  = existsFrom({"/etc/ssl/certs/ca-certificates.crt"});
		const auto r = ca::discoverWith(env, ex);
		CHECK(r.file == "/etc/ssl/certs/ca-certificates.crt",
		      "probe finds Debian/Arch bundle");
	}

	// 7) SteamOS/Arch case: only the extracted bundle is a real file (the
	//    /etc/ssl symlinks are assumed broken/absent) -> it is discovered.
	{
		auto env = envFrom({});
		auto ex  = existsFrom({"/etc/ca-certificates/extracted/tls-ca-bundle.pem"});
		const auto r = ca::discoverWith(env, ex);
		CHECK(r.file == "/etc/ca-certificates/extracted/tls-ca-bundle.pem",
		      "probe finds the extracted SteamOS/Arch bundle");
	}

	// 8) Fedora/RHEL bundle path is in the probe list.
	{
		auto env = envFrom({});
		auto ex  = existsFrom({"/etc/pki/tls/certs/ca-bundle.crt"});
		const auto r = ca::discoverWith(env, ex);
		CHECK(r.file == "/etc/pki/tls/certs/ca-bundle.crt",
		      "probe finds the Fedora/RHEL bundle");
	}

	// 9) SSL_CERT_DIR honored for the CAPATH; otherwise the dir probe list.
	{
		auto env = envFrom({{"SSL_CERT_DIR", "/my/certs"}});
		auto ex  = existsFrom({"/my/certs"});
		const auto r = ca::discoverWith(env, ex);
		CHECK(r.dir == "/my/certs", "SSL_CERT_DIR honored for CAPATH");
	}
	{
		auto env = envFrom({});
		auto ex  = existsFrom({"/etc/ssl/certs"});
		const auto r = ca::discoverWith(env, ex);
		CHECK(r.dir == "/etc/ssl/certs", "dir probe finds /etc/ssl/certs");
	}

	// 10) Nothing present anywhere -> both empty (caller leaves curl on its
	//     compiled-in default rather than pointing it at a bogus path).
	{
		auto env = envFrom({});
		auto ex  = existsFrom({});
		const auto r = ca::discoverWith(env, ex);
		CHECK(r.file.empty() && r.dir.empty(),
		      "no trust store anywhere -> empty (curl keeps its default)");
	}

	if (g_failures == 0) { std::printf("\nALL PASS\n"); return 0; }
	std::printf("\n%d CHECK(S) FAILED\n", g_failures);
	return 1;
}
