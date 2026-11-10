// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class GameMessage : ModuleRules {
    public GameMessage(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "GameplayTags",
            "GameCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
        });

        PublicIncludePaths.AddRange(new string[] {
            ModuleDirectory,
            Path.Combine(ModuleDirectory, "Public"),
        });
    }
}
