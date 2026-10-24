// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class GameGameplayCore : ModuleRules {
    public GameGameplayCore(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "GameplayTags",
            "GameCore",
            "NetCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "GameLogging",
        });

        PublicIncludePaths.AddRange(new string[] {
            ModuleDirectory,
            Path.Combine(ModuleDirectory, "Public"),
        });
    }
}
