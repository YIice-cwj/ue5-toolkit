// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameLogRegistryTest.cpp
 * @brief FGameLogRegistry 与 FModuleLogRegistrar 生命周期、常驻槽位、大小写精确匹配、动态扩容与高并发测试
 */

#include "Logging/GameLogCategories.h"

#if WITH_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "Containers/StringConv.h"
#include "Logging/FModuleLogRegistrar.h"
#include "Misc/AutomationTest.h"
#include "Misc/CString.h"
#include "Types/TCancelableScopeGuard.h"

#include <atomic>

namespace {

DEFINE_LOG_CATEGORY_STATIC(LogRegistryCatA, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogRegistryCatB, Log, All);

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLogRegistryLifecycleTest,
    "Game.Logging.Registry.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证日志分类注册、查询、大小写敏感匹配、覆写、注销与常驻槽位原子同步机制
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过时返回 true
 */
bool FGameLogRegistryLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const char* ModuleName = "UnitTest_LifecycleModule";
    const char* LowercaseModuleName = "unittest_lifecyclemodule";
    const auto Cleanup = MakeCancelableScopeGuard([ModuleName, LowercaseModuleName]() {
        Game::Log::FGameLogRegistry::UnregisterCategory(ModuleName);
        Game::Log::FGameLogRegistry::UnregisterCategory(LowercaseModuleName);
    });

    FLogCategoryBase& Fallback = Game::Log::FGameLogRegistry::GetFallbackCategory();

    TestEqual(TEXT("未注册模块应返回 LogGame 兜底分类"), &Game::Log::FGameLogRegistry::GetCategory(ModuleName), &Fallback);
    TestEqual(TEXT("空指针模块名应返回 LogGame 兜底分类"), &Game::Log::FGameLogRegistry::GetCategory(nullptr), &Fallback);
    TestEqual(TEXT("空字符串模块名应返回 LogGame 兜底分类"), &Game::Log::FGameLogRegistry::GetCategory(""), &Fallback);

    const Game::Log::FLogCategorySlot* NullSlot = Game::Log::FGameLogRegistry::ResolveCategorySlot(nullptr);
    const Game::Log::FLogCategorySlot* EmptySlot = Game::Log::FGameLogRegistry::ResolveCategorySlot("");
    TestNotNull(TEXT("空指针模块名槽位不得为空"), NullSlot);
    TestNotNull(TEXT("空字符串模块名槽位不得为空"), EmptySlot);
    if (NullSlot != nullptr && EmptySlot != nullptr) {
        TestEqual(TEXT("空指针模块名槽位应指向兜底分类"), NullSlot->load(std::memory_order_acquire), &Fallback);
        TestEqual(TEXT("空字符串模块名槽位应指向兜底分类"), EmptySlot->load(std::memory_order_acquire), &Fallback);
    }

    const Game::Log::FLogCategorySlot* ModuleSlot = Game::Log::FGameLogRegistry::ResolveCategorySlot(ModuleName);
    TestNotNull(TEXT("未注册模块也应能预解析出常驻槽位"), ModuleSlot);
    if (ModuleSlot == nullptr) {
        return false;
    }
    TestEqual(TEXT("预解析槽位初始值应为兜底分类"), ModuleSlot->load(std::memory_order_acquire), &Fallback);

    Game::Log::FGameLogRegistry::RegisterCategory("", &LogRegistryCatA);
    Game::Log::FGameLogRegistry::RegisterCategory(nullptr, &LogRegistryCatA);
    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, nullptr);
    Game::Log::FGameLogRegistry::UnregisterCategory("");
    Game::Log::FGameLogRegistry::UnregisterCategory(nullptr);
    Game::Log::FGameLogRegistry::UnregisterCategory("UnitTest_NonExistentModule");
    TestEqual(TEXT("无效入参注册与注销不得改变模块槽位状态"), ModuleSlot->load(std::memory_order_acquire), &Fallback);

    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogRegistryCatA);
    TestEqual(
        TEXT("注册后 GetCategory 应返回分类 A"),
        &Game::Log::FGameLogRegistry::GetCategory(ModuleName),
        static_cast<FLogCategoryBase*>(&LogRegistryCatA)
    );
    TestEqual(
        TEXT("注册后已缓存的常驻槽位应即时指向分类 A"),
        ModuleSlot->load(std::memory_order_acquire),
        static_cast<FLogCategoryBase*>(&LogRegistryCatA)
    );
    TestEqual(
        TEXT("重复解析常驻槽位必须返回完全相同的地址"),
        Game::Log::FGameLogRegistry::ResolveCategorySlot(ModuleName),
        ModuleSlot
    );
    TestEqual(
        TEXT("模块名匹配必须严格区分大小写"),
        &Game::Log::FGameLogRegistry::GetCategory(LowercaseModuleName),
        &Fallback
    );

    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogRegistryCatA);
    TestEqual(
        TEXT("幂等重复注册相同分类应保持分类 A"),
        ModuleSlot->load(std::memory_order_acquire),
        static_cast<FLogCategoryBase*>(&LogRegistryCatA)
    );

    Game::Log::FGameLogRegistry::RegisterCategory(ModuleName, &LogRegistryCatB);
    TestEqual(
        TEXT("覆写后应获取新日志分类 B"),
        &Game::Log::FGameLogRegistry::GetCategory(ModuleName),
        static_cast<FLogCategoryBase*>(&LogRegistryCatB)
    );
    TestEqual(
        TEXT("覆写后常驻槽位应同步指向分类 B"),
        ModuleSlot->load(std::memory_order_acquire),
        static_cast<FLogCategoryBase*>(&LogRegistryCatB)
    );

    Game::Log::FGameLogRegistry::UnregisterCategory(ModuleName);
    TestEqual(TEXT("注销后 GetCategory 应重新回退至兜底分类"), &Game::Log::FGameLogRegistry::GetCategory(ModuleName), &Fallback);
    TestEqual(TEXT("注销后常驻槽位应原子回退至兜底分类"), ModuleSlot->load(std::memory_order_acquire), &Fallback);

    Game::Log::FGameLogRegistry::UnregisterCategory(ModuleName);
    TestEqual(TEXT("重复注销已注销模块应保持幂等安全"), ModuleSlot->load(std::memory_order_acquire), &Fallback);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLogRegistrarRaiiTest,
    "Game.Logging.Registry.RegistrarRAII",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FModuleLogRegistrar 的自动登记、多层嵌套恢复、无效参数保护与外部修改防误覆写
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过时返回 true
 */
bool FGameLogRegistrarRaiiTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const char* ScopedModuleName = "UnitTest_ScopedModule";
    const char* OverriddenModule = "UnitTest_OverriddenScope";
    const char* ExternalUnregModule = "UnitTest_ExternalUnregScope";
    const auto Cleanup = MakeCancelableScopeGuard([ScopedModuleName, OverriddenModule, ExternalUnregModule]() {
        Game::Log::FGameLogRegistry::UnregisterCategory(ScopedModuleName);
        Game::Log::FGameLogRegistry::UnregisterCategory(OverriddenModule);
        Game::Log::FGameLogRegistry::UnregisterCategory(ExternalUnregModule);
    });

    FLogCategoryBase& Fallback = Game::Log::FGameLogRegistry::GetFallbackCategory();
    TestEqual(TEXT("未进入作用域前应为兜底分类"), &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName), &Fallback);

    {
        Game::Log::FModuleLogRegistrar EmptyNameRegistrar("", &LogRegistryCatA);
        Game::Log::FModuleLogRegistrar NullNameRegistrar(nullptr, &LogRegistryCatA);
        TestEqual(TEXT("空模块名注册器不得覆盖兜底分类"), &Game::Log::FGameLogRegistry::GetCategory(""), &Fallback);
    }

    {
        Game::Log::FModuleLogRegistrar ScopedRegistrar(ScopedModuleName, &LogRegistryCatA);
        TestEqual(
            TEXT("RAII 作用域内应成功注册分类 A"),
            &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
            static_cast<FLogCategoryBase*>(&LogRegistryCatA)
        );

        {
            Game::Log::FModuleLogRegistrar NullCategoryRegistrar(ScopedModuleName, nullptr);
        }
        TestEqual(
            TEXT("空分类注册器析构不得误删已有模块注册"),
            &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
            static_cast<FLogCategoryBase*>(&LogRegistryCatA)
        );

        {
            Game::Log::FModuleLogRegistrar SameCategoryRegistrar(ScopedModuleName, &LogRegistryCatA);
            TestEqual(
                TEXT("嵌套相同分类注册器应保持分类 A"),
                &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
                static_cast<FLogCategoryBase*>(&LogRegistryCatA)
            );
        }
        TestEqual(
            TEXT("嵌套相同分类析构后应依然为分类 A"),
            &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
            static_cast<FLogCategoryBase*>(&LogRegistryCatA)
        );

        {
            Game::Log::FModuleLogRegistrar NestedRegistrar(ScopedModuleName, &LogRegistryCatB);
            TestEqual(
                TEXT("嵌套 Registrar 应注册分类 B"),
                &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
                static_cast<FLogCategoryBase*>(&LogRegistryCatB)
            );
        }
        TestEqual(
            TEXT("嵌套 Registrar 析构后应恢复外层分类 A"),
            &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
            static_cast<FLogCategoryBase*>(&LogRegistryCatA)
        );
    }

    TestEqual(TEXT("离开 RAII 作用域后应自动注销并回退至兜底分类"), &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName), &Fallback);

    {
        Game::Log::FModuleLogRegistrar OuterFallbackRegistrar(ScopedModuleName, &Fallback);
        {
            Game::Log::FModuleLogRegistrar InnerFallbackRegistrar(ScopedModuleName, &Fallback);
            TestEqual(TEXT("嵌套注册兜底分类时应保持兜底分类"), &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName), &Fallback);
        }
        TestEqual(TEXT("内层退出后外层仍保持有效"), &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName), &Fallback);
    }

    {
        {
            Game::Log::FModuleLogRegistrar OuterRegistrar(OverriddenModule, &LogRegistryCatA);
            Game::Log::FGameLogRegistry::RegisterCategory(OverriddenModule, &LogRegistryCatB);
        }
        TestEqual(
            TEXT("失效的 Registrar 析构不得覆盖外部更新的分类 B"),
            &Game::Log::FGameLogRegistry::GetCategory(OverriddenModule),
            static_cast<FLogCategoryBase*>(&LogRegistryCatB)
        );
    }

    {
        {
            Game::Log::FModuleLogRegistrar OuterRegistrar(ExternalUnregModule, &LogRegistryCatA);
            Game::Log::FGameLogRegistry::UnregisterCategory(ExternalUnregModule);
        }
        TestEqual(
            TEXT("已被外部注销的 Registrar 析构应安全幂等保持兜底分类"),
            &Game::Log::FGameLogRegistry::GetCategory(ExternalUnregModule),
            &Fallback
        );
    }

    {
        Game::Log::FGameLogRegistry::RegisterCategory(ScopedModuleName, &LogRegistryCatA);
        {
            Game::Log::FModuleLogRegistrar OverlayRegistrar(ScopedModuleName, &LogRegistryCatB);
            TestEqual(
                TEXT("覆盖已有手动注册分类时应切换至分类 B"),
                &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
                static_cast<FLogCategoryBase*>(&LogRegistryCatB)
            );
        }
        TestEqual(
            TEXT("覆盖注册器析构后应准确恢复外层手动注册的分类 A"),
            &Game::Log::FGameLogRegistry::GetCategory(ScopedModuleName),
            static_cast<FLogCategoryBase*>(&LogRegistryCatA)
        );
        Game::Log::FGameLogRegistry::UnregisterCategory(ScopedModuleName);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLogRegistryDynamicCapacityTest,
    "Game.Logging.Registry.DynamicCapacityAndConcurrency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 200+ 模块动态扩容、调用方缓冲区改写隔离以及多线程并发注册/查询/槽位解析安全性
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过时返回 true
 */
bool FGameLogRegistryDynamicCapacityTest::RunTest([[maybe_unused]] const FString& Parameters) {
    constexpr int32 ModuleCount = 200;

    TArray<FString> ModuleNames;
    ModuleNames.Reserve(ModuleCount);
    for (int32 Index = 0; Index < ModuleCount; ++Index) {
        ModuleNames.Add(FString::Printf(TEXT("UnitTest_BulkModule_%d"), Index));
    }

    const auto Cleanup = MakeCancelableScopeGuard([&ModuleNames]() {
        for (const FString& Name : ModuleNames) {
            FTCHARToUTF8 AnsiName(*Name);
            Game::Log::FGameLogRegistry::UnregisterCategory(AnsiName.Get());
        }
        Game::Log::FGameLogRegistry::UnregisterCategory("UnitTest_BufferNameA");
        Game::Log::FGameLogRegistry::UnregisterCategory("UnitTest_BufferNameB");
    });

    for (int32 Index = 0; Index < ModuleCount; ++Index) {
        FTCHARToUTF8 AnsiName(*ModuleNames[Index]);
        FLogCategoryBase* Category = (Index % 2 == 0)
            ? static_cast<FLogCategoryBase*>(&LogRegistryCatA)
            : static_cast<FLogCategoryBase*>(&LogRegistryCatB);
        Game::Log::FGameLogRegistry::RegisterCategory(AnsiName.Get(), Category);
    }

    bool bAllResolved = true;
    for (int32 Index = 0; Index < ModuleCount; ++Index) {
        FTCHARToUTF8 AnsiName(*ModuleNames[Index]);
        FLogCategoryBase* Expected = (Index % 2 == 0)
            ? static_cast<FLogCategoryBase*>(&LogRegistryCatA)
            : static_cast<FLogCategoryBase*>(&LogRegistryCatB);
        if (&Game::Log::FGameLogRegistry::GetCategory(AnsiName.Get()) != Expected) {
            bAllResolved = false;
            break;
        }
    }
    TestTrue(TEXT("超过 128 上限的 200 个模块全部注册并正确解析"), bAllResolved);

    ANSICHAR SharedBuffer[64];
    FCStringAnsi::Strncpy(SharedBuffer, "UnitTest_BufferNameA", UE_ARRAY_COUNT(SharedBuffer));
    Game::Log::FGameLogRegistry::RegisterCategory(SharedBuffer, &LogRegistryCatA);
    FCStringAnsi::Strncpy(SharedBuffer, "UnitTest_BufferNameB", UE_ARRAY_COUNT(SharedBuffer));
    Game::Log::FGameLogRegistry::RegisterCategory(SharedBuffer, &LogRegistryCatB);

    TestEqual(
        TEXT("调用方缓冲区改写后名称 A 仍可解析（名称已深拷贝）"),
        &Game::Log::FGameLogRegistry::GetCategory("UnitTest_BufferNameA"),
        static_cast<FLogCategoryBase*>(&LogRegistryCatA)
    );
    TestEqual(
        TEXT("调用方缓冲区改写后名称 B 可解析"),
        &Game::Log::FGameLogRegistry::GetCategory("UnitTest_BufferNameB"),
        static_cast<FLogCategoryBase*>(&LogRegistryCatB)
    );

    std::atomic<int32> ConcurrentErrors{0};
    ParallelFor(8, [&ModuleNames, &ConcurrentErrors](int32 WorkerIdx) {
        for (int32 Step = 0; Step < ModuleCount; ++Step) {
            FTCHARToUTF8 AnsiName(*ModuleNames[Step]);
            const Game::Log::FLogCategorySlot* Slot = Game::Log::FGameLogRegistry::ResolveCategorySlot(AnsiName.Get());
            if (Slot == nullptr || Slot->load(std::memory_order_acquire) == nullptr) {
                ConcurrentErrors.fetch_add(1, std::memory_order_relaxed);
            }
            if ((WorkerIdx + Step) % 16 == 0) {
                FLogCategoryBase* Cat = (Step % 2 == 0)
                    ? static_cast<FLogCategoryBase*>(&LogRegistryCatA)
                    : static_cast<FLogCategoryBase*>(&LogRegistryCatB);
                Game::Log::FGameLogRegistry::RegisterCategory(AnsiName.Get(), Cat);
            }
        }
    });
    TestEqual(TEXT("多线程并发槽位解析与注册不得产生任何空指针或竞态错误"), ConcurrentErrors.load(), 0);

    for (int32 Index = 0; Index < ModuleCount; ++Index) {
        FTCHARToUTF8 AnsiName(*ModuleNames[Index]);
        Game::Log::FGameLogRegistry::UnregisterCategory(AnsiName.Get());
    }

    FLogCategoryBase& Fallback = Game::Log::FGameLogRegistry::GetFallbackCategory();
    bool bAllReleased = true;
    for (int32 Index = 0; Index < ModuleCount; ++Index) {
        FTCHARToUTF8 AnsiName(*ModuleNames[Index]);
        if (&Game::Log::FGameLogRegistry::GetCategory(AnsiName.Get()) != &Fallback) {
            bAllReleased = false;
            break;
        }
    }
    TestTrue(TEXT("全部注销后统一回落兜底分类"), bAllReleased);

    return true;
}

#endif // WITH_AUTOMATION_TESTS
