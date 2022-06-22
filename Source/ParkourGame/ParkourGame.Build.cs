// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ParkourGame : ModuleRules
{
	public ParkourGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay" });
	}
}
