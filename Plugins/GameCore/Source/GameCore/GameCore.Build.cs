// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GameCore : ModuleRules {
    public GameCore(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "GameplayTags",
        });
    }
}
