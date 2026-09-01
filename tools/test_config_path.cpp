// Standalone regression test for the canonical SteamTools config directory.
//
// The runtime config reader and cache lock must resolve XDG_CONFIG_HOME in
// exactly the same way. In particular, an empty XDG_CONFIG_HOME is treated
// like an unset value and falls back to $HOME/.config, not /SteamTools.

#include "../src/config_path.hpp"

#include <cstdio>

static int g_failures = 0;

static void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", message);
        ++g_failures;
    }
    else
    {
        std::printf("ok:   %s\n", message);
    }
}

int main()
{
    using ConfigPath::steamtoolsConfigDir;

    check(steamtoolsConfigDir(nullptr, "/home/tester") ==
              "/home/tester/.config/SteamTools",
          "unset XDG_CONFIG_HOME uses the HOME config directory");
    check(steamtoolsConfigDir("", "/home/tester") ==
              "/home/tester/.config/SteamTools",
          "empty XDG_CONFIG_HOME uses the HOME config directory");
    check(steamtoolsConfigDir("/tmp/xdg", "/home/tester") ==
              "/tmp/xdg/SteamTools",
          "nonempty XDG_CONFIG_HOME is used as the config directory base");

    if (g_failures == 0) std::puts("all config-path checks passed");
    else                 std::printf("%d config-path check(s) FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
