// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GameData : ModuleRules {
    public GameData(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "Json",
            "JsonUtilities",
            "GameplayTags",
            "GameLogging",
            "GameCore",
            "GameDataCore",
        });
    }
}
