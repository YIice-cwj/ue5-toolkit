// Copyright Epic Games, Inc. All Rights Reserved.

#include "Logging/GameLogCategories.h"

#if WITH_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "Containers/StringConv.h"
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
 */
bool FGameLogRegistryLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const char* ModuleName = "UnitTest_LifecycleModule";

    TestEqual(TEXT("未注册模块应返回 LogGame 兜底分类"),
        &Game::Log::FGameLogRegistry::GetCategory(ModuleName),
        &Game::Log::FGameLogRegistry::GetFallbackCategory());

    TestEqual(TEXT("空指针模块名应返回 LogGame 兜底分类"),
        &Game::Log::FGameLogRegistry::GetCategory(nullptr),
        &Game::Log::FGameLogRegistry::GetFallbackCategory());

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
 * @brief 验证 FModuleLogRegistrar 的 RAII 自动登记、空分类保护与析构注销
 */
bool FGameLogRegistrarRaiiTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const char* ScopedModuleName = "UnitTest_ScopedModule";

    TestEqual(TEXT("未进入作用域前应为兜底分类"),
        &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
        &Game::Log::FGameLogRegistry::GetFallbackCategory());

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
    }

    TestEqual(TEXT("离开 RAII 作用域后应自动注销并回退至兜底分类"),
        &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
        &Game::Log::FGameLogRegistry::GetFallbackCategory());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameLogMacrosConcurrencyTest, "Game.Logging.Macros.Concurrency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证 GAME_LOG、GAME_LOG_ONCE 与 GAME_LOG_THROTTLE 在多线程并发调用下的安全性与状态完整性
 */
bool FGameLogMacrosConcurrencyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    constexpr int32 WorkerCount = 64;
    std::atomic<int32> CompletedWorkers{0};

    ParallelFor(WorkerCount, [&CompletedWorkers](int32 Index) {
        GAME_LOG(Log, TEXT("Parallel log emission index: %d"), Index);
        GAME_CLOG(Index % 2 == 0, Log, TEXT("Parallel clog emission index: %d"), Index);
        GAME_LOG_ONCE(Log, TEXT("Parallel once emission (should only appear once)"));
        GAME_CLOG_ONCE(Index >= 0, Log, TEXT("Parallel clog_once emission (should only appear once)"));
        GAME_LOG_THROTTLE(10.0, Log, TEXT("Parallel throttle emission (interval 10s)"));
        CompletedWorkers.fetch_add(1, std::memory_order_relaxed);
    });

    TestEqual(TEXT("全部并发工作线程均应安全完成日志宏调用"),
        CompletedWorkers.load(std::memory_order_relaxed),
        WorkerCount);
    TestEqual(TEXT("并发调用后当前模块分类仍应保持有效"),
        &Game::Log::FGameLogRegistry::GetCategory(UE_MODULE_NAME),
        &Game::Log::FGameLogRegistry::GetFallbackCategory());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameLogDynamicDispatchEpochTest, "Game.Logging.Macros.EpochDispatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证动态分发在模块注册覆写与注销恢复时通过 Epoch 机制成功自愈更新宏缓存，且结束后恢复模块默认分类
 */
bool FGameLogDynamicDispatchEpochTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const char* ModuleName = UE_MODULE_NAME;
    FLogCategoryBase& FallbackCat = Game::Log::FGameLogRegistry::GetFallbackCategory();

    GAME_LOG(Log, TEXT("Epoch dispatch baseline emission"));
    const int32 EpochInitial = Game::Log::FGameLogRegistry::GetEpoch();

    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogUnitTestCatA);
    GAME_LOG(Log, TEXT("Epoch dispatch overridden emission"));
    FLogCategoryBase& AfterRegCat = Game::Log::FGameLogRegistry::GetCategory(ModuleName);

    TestEqual(TEXT("注册后应获取到 LogUnitTestCatA"),
        &AfterRegCat,
        static_cast<FLogCategoryBase*>(&LogUnitTestCatA));
    TestTrue(TEXT("覆写分类后纪元号应递增"),
        Game::Log::FGameLogRegistry::GetEpoch() > EpochInitial);

    Game::Log::FGameLogRegistry::UnregisterCategory(ModuleName);
    GAME_LOG(Log, TEXT("Epoch dispatch fallback emission"));
    FLogCategoryBase& RestoredCat = Game::Log::FGameLogRegistry::GetCategory(ModuleName);

    TestEqual(TEXT("注销后恢复为 Fallback 分类"),
        &RestoredCat,
        &FallbackCat);

    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogGame);
    TestEqual(TEXT("测试结束后应恢复 GameLogging 模块默认分类注册"),
        &Game::Log::FGameLogRegistry::GetCategory(ModuleName),
        static_cast<FLogCategoryBase*>(&LogGame));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameLogRegistryDynamicCapacityTest, "Game.Logging.Registry.DynamicCapacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 动态容量与名称拷贝：注册表不再受固定 128 上限约束，且不持调用方临时缓冲区
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

