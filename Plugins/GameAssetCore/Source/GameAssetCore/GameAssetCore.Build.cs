// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GameAssetCore : ModuleRules {
    public GameAssetCore(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "GameCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
        });
    }
}
