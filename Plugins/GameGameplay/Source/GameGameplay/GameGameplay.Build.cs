// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class GameGameplay : ModuleRules {
    public GameGameplay(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "GameplayAbilities",
            "GameplayTags",
            "GameCore",
            "GameGameplayCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "GameplayTasks",
            "GameLogging",
        });

        PublicIncludePaths.AddRange(new string[] {
            ModuleDirectory,
            Path.Combine(ModuleDirectory, "Public"),
        });
    }
}
