// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CollegeCampusGame : ModuleRules
{
	public CollegeCampusGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule", "NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"HTTP",
			"Json",
			"AnimationCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "OnlineSubsystem", "OnlineSubsystemUtils" });

		// FPlatformApplicationMisc::ClipboardCopy - the profile page's COPY (player ID) and SHARE.
		// Slate pulls ApplicationCore in only when bCompileAgainstApplicationCore is set, so naming it
		// here is what actually links the symbol (otherwise: LNK2019 ClipboardCopy, module-wide).
		PrivateDependencyModuleNames.Add("ApplicationCore");

		// Campus/CampusSocial.cpp holds ONE WebSocket to /v1/social/ws for invites, party roster and
		// presence. It is not a nicety: the API lives behind an ngrok free tunnel metered at 20,000 HTTP
		// requests a MONTH, and a 15-second poll would burn that in four hours with twenty testers, while
		// a socket costs one request per connection and makes heartbeats and acks free frames
		// (docs/SOCIAL_PARTY_DESIGN.md §3.4). libWebSockets ships prebuilt for Android arm64, so this is
		// one line; everything that touches IWebSocket is still behind #if WITH_WEBSOCKETS so a platform
		// without it compiles and falls back to GET /v1/social/events.
		PrivateDependencyModuleNames.Add("WebSockets");

		// Campus/CampusMapArt.cpp decodes the map-select banner PNGs at runtime (they ship as loose
		// UFS files, see DefaultGame.ini) instead of cooking a texture asset per picture.
		// Campus/CampusAvatar.cpp needs it too: profile pictures (preset icons and uploaded photos)
		// arrive over HTTP as JPEG / PNG bytes and are decoded into transient UI textures.
		PrivateDependencyModuleNames.Add("ImageWrapper");

		if (Target.bBuildEditor)
		{
			// EDITOR ONLY: Campus/CampusAvatarPicker.cpp opens an ordinary open-file dialog on Windows so
			// the whole profile-picture path (pick -> downscale -> JPEG -> PUT -> decode -> badge) can be
			// tested without a phone. On the phone the picker is the Android system photo picker and this
			// module is not referenced at all.
			PrivateDependencyModuleNames.Add("DesktopPlatform");
		}

		// Internet play (Campus/CampusEOS.cpp): EOS Connect device-id login, so a match hosted on a
		// phone can be reached over EOS P2P instead of a LAN address. OnlineServicesInterface is the
		// v2 online stack (it owns the device-id login AND registers the EOS socket subsystem that
		// NetDriverEOS needs); EOSShared + EOSSDK are only for EOS_Connect_CreateDeviceId and for
		// reading this phone's ProductUserId back out. OnlineSubsystemNull stays the default platform
		// service - voice and Google Play billing are untouched by any of this.
		PrivateDependencyModuleNames.AddRange(new string[] { "OnlineServicesInterface", "EOSShared", "EOSSDK" });

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			// Plain HTTP to the LAN test server; drop when the API moves behind HTTPS.
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", System.IO.Path.Combine(ModuleDirectory, "CampusCombat_UPL.xml"));
			// Android JNI (Razorpay payment sheet bridge, Campus/CampusRazorpay.cpp)
			PrivateDependencyModuleNames.Add("Launch");
			PrivateDependencyModuleNames.Add("AndroidPermission");
		}

		PublicIncludePaths.AddRange(new string[] {
			"CollegeCampusGame",
			"CollegeCampusGame/Variant_Platforming",
			"CollegeCampusGame/Variant_Platforming/Animation",
			"CollegeCampusGame/Variant_Combat",
			"CollegeCampusGame/Variant_Combat/AI",
			"CollegeCampusGame/Variant_Combat/Animation",
			"CollegeCampusGame/Variant_Combat/Gameplay",
			"CollegeCampusGame/Variant_Combat/Interfaces",
			"CollegeCampusGame/Variant_Combat/UI",
			"CollegeCampusGame/Variant_SideScrolling",
			"CollegeCampusGame/Variant_SideScrolling/AI",
			"CollegeCampusGame/Variant_SideScrolling/Gameplay",
			"CollegeCampusGame/Variant_SideScrolling/Interfaces",
			"CollegeCampusGame/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
