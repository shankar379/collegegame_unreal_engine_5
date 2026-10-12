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
			"AIModule",
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
