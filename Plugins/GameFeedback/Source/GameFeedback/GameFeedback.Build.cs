// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class GameFeedback : ModuleRules {
    public GameFeedback(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "GameplayTags",
            "Niagara",
            "DeveloperSettings",
            "GameCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "GameLogging",
            "GameMessage",
            "NetCore",
        });

        PublicIncludePaths.AddRange(new string[] {
            Path.Combine(ModuleDirectory, "Public"),
        });
    }
}

