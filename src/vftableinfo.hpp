#pragma once

namespace VFTIndexes
{
	namespace IClientEngine
	{
		constexpr int GetClientUser = 7;
	}

	namespace IClientApps
	{
		constexpr int GetAppData = 0;
		constexpr int GetAppDataSection = 5;
		constexpr int RequestAppInfoUpdate = 7;
		constexpr int GetDLCCount = 8;
		constexpr int GetDLCDataByIndex = 9;
		constexpr int GetAppType = 10;
	}

	namespace IClientCompat
	{
		// CCompatManager's Linux i386 vtable.  The 2026-10 client (version
		// 1791918634) removed three methods ahead of these (IClientCompatMap
		// 19 -> 16 slots), moving them from 4/7 to 1/4.  Verified by identical
		// slot bodies and by the IPC method order of IClientCompatMap;
		// test_clientcompat checks both slots against an installed client.
		constexpr int SpecifyCompatTool = 1;
		constexpr int GetCompatToolName = 4;
	}

	namespace IClientAppManager
	{
		constexpr int InstallApp = 0;
		constexpr int UninstallApp = 1;
		constexpr int LaunchApp = 2;
		constexpr int GetAppInstallState = 4;
		constexpr int IsAppDlcInstalled = 9;
		constexpr int BIsDlcEnabled = 11;
		constexpr int GetUpdateInfo = 20;
	}

	namespace IClientRemoteStorage
	{
		constexpr int IsCloudEnabledForApp = 24;
	}

	namespace IClientUser
	{
		constexpr int BLoggedOn = 4;
		constexpr int GetSteamID = 10;
		// Linux i386 IClientUserMap; the wrapper carries IPC hash 0xE0468CB4.
		constexpr int GetEncryptedAppTicket = 121;
	}

	namespace IClientUtils
	{
		constexpr int GetOfflineMode = 17;
		constexpr int GetAppId = 19;
	}
}
