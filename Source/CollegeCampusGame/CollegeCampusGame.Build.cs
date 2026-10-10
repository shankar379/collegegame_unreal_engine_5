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
			"Json"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			// Plain HTTP to the LAN test server; drop when the API moves behind HTTPS.
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", System.IO.Path.Combine(ModuleDirectory, "CampusCombat_UPL.xml"));
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
