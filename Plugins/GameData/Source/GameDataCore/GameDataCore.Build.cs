// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GameDataCore : ModuleRules {
    public GameDataCore(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "Json",
            "JsonUtilities",
            "GameLogging",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "GameCore",
        });
    }
}
