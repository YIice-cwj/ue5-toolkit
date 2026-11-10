// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

/**
 * @brief GameCore 模块构建规则定义
 *
 * 注意：保持 UseExplicitOrSharedPCHs 以确保共享 PCH 中的 WITH_AUTOMATION_TESTS 等全局编译宏
 * 在所有源码与单测翻译单元编译期一致可见（若切换为 NoPCHs 则必须在单测头文件前显式包含 Misc/AutomationTest.h）。
 */
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
