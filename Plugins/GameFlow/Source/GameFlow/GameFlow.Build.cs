// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameFlow.Build.cs
 * @brief GameFlow 关卡流转与异步就绪屏障模块构建规则定义
 */

using UnrealBuildTool;

public class GameFlow : ModuleRules {
    public GameFlow(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "GameCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "GameLogging",
        });
    }
}
