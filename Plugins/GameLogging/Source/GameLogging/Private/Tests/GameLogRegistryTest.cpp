// Copyright Epic Games, Inc. All Rights Reserved.

#include "Logging/GameLogCategories.h"

#if WITH_AUTOMATION_TESTS

#include "Containers/StringConv.h"
#include "Logging/FModuleLogRegistrar.h"
#include "Misc/AutomationTest.h"
#include "Misc/CString.h"

namespace {

DEFINE_LOG_CATEGORY_STATIC(LogRegistryCatA, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogRegistryCatB, Log, All);

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameLogRegistryLifecycleTest, "Game.Logging.Registry.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证日志分类注册、查询、覆写、注销与纪元号自增机制
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过时返回 true，否则返回 false
 */
bool FGameLogRegistryLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const char* ModuleName = "UnitTest_LifecycleModule";

    TestEqual(TEXT("未注册模块应返回 LogGame 兜底分类"),
        &Game::Log::FGameLogRegistry::GetCategory(ModuleName),
        &Game::Log::FGameLogRegistry::GetFallbackCategory());

    TestEqual(TEXT("空指针模块名应返回 LogGame 兜底分类"),
        &Game::Log::FGameLogRegistry::GetCategory(nullptr),
        &Game::Log::FGameLogRegistry::GetFallbackCategory());

    const int32 EpochBeforeEmpty = Game::Log::FGameLogRegistry::GetEpoch();
    Game::Log::FGameLogRegistry::RegisterCategory("", &LogRegistryCatA);
    Game::Log::FGameLogRegistry::RegisterCategory(nullptr, &LogRegistryCatA);
    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, nullptr);
    TestEqual(TEXT("空字符串模块名应返回 LogGame 兜底分类"),
        &Game::Log::FGameLogRegistry::GetCategory(""),
        &Game::Log::FGameLogRegistry::GetFallbackCategory());
    TestEqual(TEXT("无效参数注册不得推进全局纪元号"),
        Game::Log::FGameLogRegistry::GetEpoch(), EpochBeforeEmpty);
    Game::Log::FGameLogRegistry::UnregisterCategory("");
    Game::Log::FGameLogRegistry::UnregisterCategory(nullptr);
    Game::Log::FGameLogRegistry::UnregisterCategory("UnitTest_NonExistentModule");
    TestEqual(TEXT("无效参数或不存在模块注销不得推进全局纪元号"),
        Game::Log::FGameLogRegistry::GetEpoch(), EpochBeforeEmpty);

    const int32 EpochBeforeReg = Game::Log::FGameLogRegistry::GetEpoch();
    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogRegistryCatA);

    TestEqual(TEXT("注册后应正确获取目标日志分类 A"),
        &Game::Log::FGameLogRegistry::GetCategory(ModuleName),
        static_cast<FLogCategoryBase*>(&LogRegistryCatA));

    TestTrue(TEXT("注册后纪元号应自增"),
        Game::Log::FGameLogRegistry::GetEpoch() > EpochBeforeReg);

    const int32 EpochBeforeIdentical = Game::Log::FGameLogRegistry::GetEpoch();
    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogRegistryCatA);
    TestEqual(TEXT("重复注册相同分类不得推进全局纪元号"),
        Game::Log::FGameLogRegistry::GetEpoch(), EpochBeforeIdentical);

    const int32 EpochBeforeOverride = Game::Log::FGameLogRegistry::GetEpoch();
    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogRegistryCatB);

    TestEqual(TEXT("覆写后应获取新日志分类 B"),
        &Game::Log::FGameLogRegistry::GetCategory(ModuleName),
        static_cast<FLogCategoryBase*>(&LogRegistryCatB));

    TestTrue(TEXT("覆写后纪元号应自增"),
        Game::Log::FGameLogRegistry::GetEpoch() > EpochBeforeOverride);

    const int32 EpochBeforeUnreg = Game::Log::FGameLogRegistry::GetEpoch();
    Game::Log::FGameLogRegistry::UnregisterCategory(ModuleName);

    TestEqual(TEXT("注销后应重新回退至 LogGame"),
        &Game::Log::FGameLogRegistry::GetCategory(ModuleName),
        &Game::Log::FGameLogRegistry::GetFallbackCategory());

    TestTrue(TEXT("注销后纪元号应自增"),
        Game::Log::FGameLogRegistry::GetEpoch() > EpochBeforeUnreg);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameLogRegistrarRaiiTest, "Game.Logging.Registry.RegistrarRAII",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证 Registrar 的自动登记、嵌套恢复、无效参数保护与析构注销
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过时返回 true，否则返回 false
 */
bool FGameLogRegistrarRaiiTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const char* ScopedModuleName = "UnitTest_ScopedModule";

    TestEqual(TEXT("未进入作用域前应为兜底分类"),
        &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
        &Game::Log::FGameLogRegistry::GetFallbackCategory());

    {
        Game::Log::FModuleLogRegistrar EmptyNameRegistrar("", &LogRegistryCatA);
        Game::Log::FModuleLogRegistrar NullNameRegistrar(nullptr, &LogRegistryCatA);
        TestEqual(TEXT("空模块名注册器不得覆盖兜底分类"),
            &Game::Log::FGameLogRegistry::GetCategory(""),
            &Game::Log::FGameLogRegistry::GetFallbackCategory());
    }

    {
        Game::Log::FModuleLogRegistrar ScopedRegistrar(ScopedModuleName, &LogRegistryCatA);
        TestEqual(TEXT("RAII 作用域内应成功注册分类 A"),
            &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
            static_cast<FLogCategoryBase*>(&LogRegistryCatA));

        {
            Game::Log::FModuleLogRegistrar NullCategoryRegistrar(ScopedModuleName, nullptr);
        }

        TestEqual(TEXT("空分类注册器析构不得误删已有模块注册"),
            &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
            static_cast<FLogCategoryBase*>(&LogRegistryCatA));

        {
            Game::Log::FModuleLogRegistrar SameCategoryRegistrar(ScopedModuleName, &LogRegistryCatA);
            TestEqual(TEXT("嵌套相同分类注册器应保持分类 A"),
                &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
                static_cast<FLogCategoryBase*>(&LogRegistryCatA));
        }

        TestEqual(TEXT("嵌套相同分类析构后应依然为分类 A"),
            &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
            static_cast<FLogCategoryBase*>(&LogRegistryCatA));

        {
            Game::Log::FModuleLogRegistrar NestedRegistrar(ScopedModuleName, &LogRegistryCatB);
            TestEqual(TEXT("嵌套 Registrar 应注册分类 B"),
                &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
                static_cast<FLogCategoryBase*>(&LogRegistryCatB));
        }

        TestEqual(TEXT("嵌套 Registrar 析构后应恢复外层分类 A"),
            &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
            static_cast<FLogCategoryBase*>(&LogRegistryCatA));
    }

    TestEqual(TEXT("离开 RAII 作用域后应自动注销并回退至兜底分类"),
        &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
        &Game::Log::FGameLogRegistry::GetFallbackCategory());

    {
        const char* OverriddenModule = "UnitTest_OverriddenScope";
        {
            Game::Log::FModuleLogRegistrar OuterRegistrar(OverriddenModule, &LogRegistryCatA);
            Game::Log::FGameLogRegistry::RegisterCategory(OverriddenModule, &LogRegistryCatB);
        }
        TestEqual(TEXT("失效的 Registrar 析构不得覆盖外部更新的分类 B"),
            &Game::Log::FGameLogRegistry::GetCategory(OverriddenModule),
            static_cast<FLogCategoryBase*>(&LogRegistryCatB));
        Game::Log::FGameLogRegistry::UnregisterCategory(OverriddenModule);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameLogRegistryDynamicCapacityTest, "Game.Logging.Registry.DynamicCapacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证动态容量突破旧固定上限且注册表自持模块名拷贝
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过时返回 true，否则返回 false
 */
bool FGameLogRegistryDynamicCapacityTest::RunTest([[maybe_unused]] const FString& Parameters) {
    constexpr int32 ModuleCount = 200;

    TArray<FString> ModuleNames;
    ModuleNames.Reserve(ModuleCount);
    for (int32 Index = 0; Index < ModuleCount; ++Index) {
        ModuleNames.Add(FString::Printf(TEXT("UnitTest_BulkModule_%d"), Index));
    }

    for (int32 Index = 0; Index < ModuleCount; ++Index) {
        FTCHARToUTF8 AnsiName(*ModuleNames[Index]);
        FLogCategoryBase* Category = (Index % 2 == 0) ? static_cast<FLogCategoryBase*>(&LogRegistryCatA)
                                                       : static_cast<FLogCategoryBase*>(&LogRegistryCatB);
        Game::Log::FGameLogRegistry::RegisterCategory(AnsiName.Get(), Category);
    }

    bool bAllResolved = true;
    for (int32 Index = 0; Index < ModuleCount; ++Index) {
        FTCHARToUTF8 AnsiName(*ModuleNames[Index]);
        FLogCategoryBase* Expected = (Index % 2 == 0) ? static_cast<FLogCategoryBase*>(&LogRegistryCatA)
                                                      : static_cast<FLogCategoryBase*>(&LogRegistryCatB);
        if (&Game::Log::FGameLogRegistry::GetCategory(AnsiName.Get()) != Expected) {
            bAllResolved = false;
            break;
        }
    }
    TestTrue(TEXT("超过旧固定上限的模块名全部注册并正确解析"), bAllResolved);

    ANSICHAR SharedBuffer[64];
    FCStringAnsi::Strncpy(SharedBuffer, "UnitTest_BufferNameA", UE_ARRAY_COUNT(SharedBuffer));
    Game::Log::FGameLogRegistry::RegisterCategory(SharedBuffer, &LogRegistryCatA);
    FCStringAnsi::Strncpy(SharedBuffer, "UnitTest_BufferNameB", UE_ARRAY_COUNT(SharedBuffer));
    Game::Log::FGameLogRegistry::RegisterCategory(SharedBuffer, &LogRegistryCatB);

    TestEqual(TEXT("调用方缓冲区改写后名称 A 仍可解析（名称已拷贝）"),
        &Game::Log::FGameLogRegistry::GetCategory("UnitTest_BufferNameA"),
        static_cast<FLogCategoryBase*>(&LogRegistryCatA));
    TestEqual(TEXT("调用方缓冲区改写后名称 B 可解析"),
        &Game::Log::FGameLogRegistry::GetCategory("UnitTest_BufferNameB"),
        static_cast<FLogCategoryBase*>(&LogRegistryCatB));
    Game::Log::FGameLogRegistry::UnregisterCategory("UnitTest_BufferNameA");
    Game::Log::FGameLogRegistry::UnregisterCategory("UnitTest_BufferNameB");

    for (int32 Index = 0; Index < ModuleCount; ++Index) {
        FTCHARToUTF8 AnsiName(*ModuleNames[Index]);
        Game::Log::FGameLogRegistry::UnregisterCategory(AnsiName.Get());
    }

    bool bAllReleased = true;
    for (int32 Index = 0; Index < ModuleCount; ++Index) {
        FTCHARToUTF8 AnsiName(*ModuleNames[Index]);
        if (&Game::Log::FGameLogRegistry::GetCategory(AnsiName.Get())
            != &Game::Log::FGameLogRegistry::GetFallbackCategory()) {
            bAllReleased = false;
            break;
        }
    }
    TestTrue(TEXT("全部注销后统一回落兜底分类"), bAllReleased);

    return true;
}

#endif // WITH_AUTOMATION_TESTS
