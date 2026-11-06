# steamtools-moon

A fork of [slsteam-moon](https://github.com/swwayps/slsteam-moon) adapted for
SteamTools, with extra protocol handlers, SteamStub support, and a Lua manifest
importer.

For building, see [`docs/BUILDING.md`](docs/BUILDING.md).

## Credits

Upstream:

- [AceSLS](https://github.com/AceSLS) and contributors - the hook framework,
  protobuf plumbing, ownership patches, family-share bypass, and the broader
  feature set this branch builds on.
- [swwayps](https://github.com/swwayps) - slsteam-moon fork.

Reference material:

- [SteamDatabase / SteamAppInfo](https://github.com/SteamDatabase/SteamAppInfo)
  - `appinfo.vdf` v41 format documentation.
- [atom0s / Steamless](https://github.com/atom0s/Steamless) - invoked by the
  wrapper-integration helper.
- [Midrags / SFF / LumaCore](https://github.com/Midrags/SFF) - Windows
  reference port for the `LoadPackage` patch logic.
- [OpenSteamTool](https://github.com/OpenSteam001/OpenSteamTool) - some
  features were ported from or inspired by this project. Full credit to its
  author.

## Support

Open an issue: https://github.com/SteamTools-Team/steamtools-moon/issues
