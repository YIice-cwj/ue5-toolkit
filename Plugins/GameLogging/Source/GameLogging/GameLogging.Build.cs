// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class GameLogging : ModuleRules {
    public GameLogging(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "GameCore",
        });

        PublicIncludePaths.AddRange(new string[] {
            Path.Combine(ModuleDirectory, "Public"),
        });
    }
}
