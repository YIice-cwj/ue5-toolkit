// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class GameDataSamples : ModuleRules {
    public GameDataSamples(ReadOnlyTargetRules Target) : base(Target) {
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
            "GameDataCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "GameCore",
        });

        PublicIncludePaths.AddRange(new string[] {
            ModuleDirectory,
            Path.Combine(ModuleDirectory, "Public"),
        });
    }
}
