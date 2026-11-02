// Copyright Epic Games, Inc. All Rights Reserved.

#include "Logging/GameLogCategories.h"

#if WITH_AUTOMATION_TESTS

#include "Containers/StringConv.h"
#include "Logging/FModuleLogRegistrar.h"
#include "Misc/AutomationTest.h"
#include "Misc/CString.h"

namespace {

DEFINE_LOG_CATEGORY_STATIC(LogUnitTestCatA, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogUnitTestCatB, Log, All);

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
    Game::Log::FGameLogRegistry::RegisterCategory("", &LogUnitTestCatA);
    TestEqual(TEXT("空字符串模块名应返回 LogGame 兜底分类"),
        &Game::Log::FGameLogRegistry::GetCategory(""),
        &Game::Log::FGameLogRegistry::GetFallbackCategory());
    TestEqual(TEXT("空字符串注册不得推进全局纪元号"),
        Game::Log::FGameLogRegistry::GetEpoch(), EpochBeforeEmpty);
    Game::Log::FGameLogRegistry::UnregisterCategory("");
    TestEqual(TEXT("空字符串注销不得推进全局纪元号"),
        Game::Log::FGameLogRegistry::GetEpoch(), EpochBeforeEmpty);

    const int32 EpochBeforeReg = Game::Log::FGameLogRegistry::GetEpoch();
    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogUnitTestCatA);

    TestEqual(TEXT("注册后应正确获取目标日志分类 A"),
        &Game::Log::FGameLogRegistry::GetCategory(ModuleName),
        static_cast<FLogCategoryBase*>(&LogUnitTestCatA));

    TestTrue(TEXT("注册后纪元号应自增"),
        Game::Log::FGameLogRegistry::GetEpoch() > EpochBeforeReg);

    const int32 EpochBeforeOverride = Game::Log::FGameLogRegistry::GetEpoch();
    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogUnitTestCatB);

    TestEqual(TEXT("覆写后应获取新日志分类 B"),
        &Game::Log::FGameLogRegistry::GetCategory(ModuleName),
        static_cast<FLogCategoryBase*>(&LogUnitTestCatB));

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
        Game::Log::FModuleLogRegistrar EmptyNameRegistrar("", &LogUnitTestCatA);
        TestEqual(TEXT("空模块名注册器不得覆盖兜底分类"),
            &Game::Log::FGameLogRegistry::GetCategory(""),
            &Game::Log::FGameLogRegistry::GetFallbackCategory());
    }

    {
        Game::Log::FModuleLogRegistrar ScopedRegistrar(ScopedModuleName, &LogUnitTestCatA);
        TestEqual(TEXT("RAII 作用域内应成功注册分类 A"),
            &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
            static_cast<FLogCategoryBase*>(&LogUnitTestCatA));

        {
            Game::Log::FModuleLogRegistrar NullCategoryRegistrar(ScopedModuleName, nullptr);
        }

        TestEqual(TEXT("空分类注册器析构不得误删已有模块注册"),
            &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
            static_cast<FLogCategoryBase*>(&LogUnitTestCatA));

        {
            Game::Log::FModuleLogRegistrar NestedRegistrar(ScopedModuleName, &LogUnitTestCatB);
            TestEqual(TEXT("嵌套 Registrar 应注册分类 B"),
                &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
                static_cast<FLogCategoryBase*>(&LogUnitTestCatB));
        }

        TestEqual(TEXT("嵌套 Registrar 析构后应恢复外层分类 A"),
            &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
            static_cast<FLogCategoryBase*>(&LogUnitTestCatA));
    }

    TestEqual(TEXT("离开 RAII 作用域后应自动注销并回退至兜底分类"),
        &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
        &Game::Log::FGameLogRegistry::GetFallbackCategory());

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
        FLogCategoryBase* Category = (Index % 2 == 0) ? static_cast<FLogCategoryBase*>(&LogUnitTestCatA)
                                                       : static_cast<FLogCategoryBase*>(&LogUnitTestCatB);
        Game::Log::FGameLogRegistry::RegisterCategory(AnsiName.Get(), Category);
    }

    bool bAllResolved = true;
    for (int32 Index = 0; Index < ModuleCount; ++Index) {
        FTCHARToUTF8 AnsiName(*ModuleNames[Index]);
        FLogCategoryBase* Expected = (Index % 2 == 0) ? static_cast<FLogCategoryBase*>(&LogUnitTestCatA)
                                                      : static_cast<FLogCategoryBase*>(&LogUnitTestCatB);
        if (&Game::Log::FGameLogRegistry::GetCategory(AnsiName.Get()) != Expected) {
            bAllResolved = false;
            break;
        }
    }
    TestTrue(TEXT("超过旧固定上限的模块名全部注册并正确解析"), bAllResolved);

    ANSICHAR SharedBuffer[64];
    FCStringAnsi::Strncpy(SharedBuffer, "UnitTest_BufferNameA", UE_ARRAY_COUNT(SharedBuffer));
    Game::Log::FGameLogRegistry::RegisterCategory(SharedBuffer, &LogUnitTestCatA);
    FCStringAnsi::Strncpy(SharedBuffer, "UnitTest_BufferNameB", UE_ARRAY_COUNT(SharedBuffer));
    Game::Log::FGameLogRegistry::RegisterCategory(SharedBuffer, &LogUnitTestCatB);

    TestEqual(TEXT("调用方缓冲区改写后名称 A 仍可解析（名称已拷贝）"),
        &Game::Log::FGameLogRegistry::GetCategory("UnitTest_BufferNameA"),
        static_cast<FLogCategoryBase*>(&LogUnitTestCatA));
    TestEqual(TEXT("调用方缓冲区改写后名称 B 可解析"),
        &Game::Log::FGameLogRegistry::GetCategory("UnitTest_BufferNameB"),
        static_cast<FLogCategoryBase*>(&LogUnitTestCatB));
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
