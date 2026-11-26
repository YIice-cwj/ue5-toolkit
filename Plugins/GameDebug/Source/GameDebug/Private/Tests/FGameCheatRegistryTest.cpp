/**
 * @file FGameCheatRegistryTest.cpp
 * @brief FGameCheatRegistry 模块化调试/GM 指令注册中心全量功能与并发自动化测试
 */

#include "Cheat/FGameCheatRegistry.h"

#include "Config/GameCompileSwitches.h"
#include "Tests/GameDebugTestUtils.h"

#if WITH_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

#include <atomic>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCheatRegistryLifecycleTest,
    "Game.Debug.Cheat.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 GM 指令注册、空白/重名/控制台冲突拦截、空引号与转义参数解析、自注销重入安全及注销清理
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameCheatRegistryLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameCheatRegistry& CheatRegistry = FGameCheatRegistry::Get();

    if constexpr (!Game::Config::bEnableGM) {
        TestFalse(
            TEXT("GM 开关关闭时注册返回 false"),
            CheatRegistry.RegisterCheat(FName(TEXT("UnitTest.GM.Noop")), TEXT("Noop"), [](const TArray<FString>&) {})
        );
        return true;
    }

    const FName GiveItemCmd(TEXT("UnitTest.GM.GiveItem"));
    const FName OneShotCmd(TEXT("UnitTest.GM.OneShotSelfRemove"));
    const FName ConflictCmd(TEXT("UnitTest.GM.EngineConflict"));
    CheatRegistry.UnregisterCheat(GiveItemCmd);
    CheatRegistry.UnregisterCheat(OneShotCmd);
    CheatRegistry.UnregisterCheat(ConflictCmd);

    const int32 BaseCount = CheatRegistry.GetCheatCount();

    TestFalse(
        TEXT("NAME_None 名称注册被拒绝"),
        CheatRegistry.RegisterCheat(NAME_None, TEXT("Invalid"), [](const TArray<FString>&) {})
    );
    TestFalse(
        TEXT("空回调注册被拒绝"),
        CheatRegistry.RegisterCheat(GiveItemCmd, TEXT("Invalid"), FGameCheatRegistry::FCheatHandler())
    );
    TestFalse(TEXT("NAME_None HasCheat 返回 false"), CheatRegistry.HasCheat(NAME_None));
    TestFalse(TEXT("NAME_None UnregisterCheat 返回 false"), CheatRegistry.UnregisterCheat(NAME_None));

    {
        Game::Debug::Test::FScopedDebugLogCapture WhitespaceLog(TEXT("包含空白或为空"));
        TestFalse(
            TEXT("首尾含空格名称被拒绝"),
            CheatRegistry.RegisterCheat(FName(TEXT("  UnitTest.GM.Padded  ")), TEXT("Bad"), [](const TArray<FString>&) {})
        );
        TestFalse(
            TEXT("中间含空格名称被拒绝"),
            CheatRegistry.RegisterCheat(FName(TEXT("UnitTest.GM BadName")), TEXT("Bad"), [](const TArray<FString>&) {})
        );
        TestEqual(TEXT("两次非法空白名称均通过 LogGameDebug 记录 Warning"), WhitespaceLog.GetTotalCount(), 2);
        TestEqual(TEXT("空白名称告警分类为 LogGameDebug"), WhitespaceLog.GetLastCategory(), FName(TEXT("LogGameDebug")));
        TestEqual(TEXT("空白名称告警级别为 Warning"), WhitespaceLog.GetLastVerbosity(), ELogVerbosity::Warning);
    }

    TArray<FString> CapturedArgs;
    const bool bRegistered = CheatRegistry.RegisterCheat(
        GiveItemCmd,
        TEXT("Grant test item by name and count"),
        [&CapturedArgs](const TArray<FString>& Args) {
            CapturedArgs = Args;
        }
    );

    TestTrue(TEXT("GM 指令首次注册成功"), bRegistered);
    TestTrue(TEXT("HasCheat 查询为 true"), CheatRegistry.HasCheat(GiveItemCmd));
    TestEqual(TEXT("注册计数递增 1"), CheatRegistry.GetCheatCount(), BaseCount + 1);
    TestNotNull(
        TEXT("IConsoleManager 已挂载同名控制台对象"),
        IConsoleManager::Get().FindConsoleObject(*GiveItemCmd.ToString())
    );

    {
        Game::Debug::Test::FScopedDebugLogCapture DupLog(TEXT("拒绝重复注册"));
        TestFalse(
            TEXT("同名 GM 指令重复注册被拦截"),
            CheatRegistry.RegisterCheat(GiveItemCmd, TEXT("Dup"), [](const TArray<FString>&) {})
        );
        TestEqual(TEXT("重复注册经 LogGameDebug 输出 1 条 Warning"), DupLog.GetTotalCount(), 1);
        TestEqual(TEXT("重复注册告警分类为 LogGameDebug"), DupLog.GetLastCategory(), FName(TEXT("LogGameDebug")));
    }

    TestTrue(
        TEXT("ExecuteCheat 命中双引号含空格参数"),
        CheatRegistry.ExecuteCheat(TEXT("UnitTest.GM.GiveItem \"Excalibur Blade\" 5"))
    );
    TestEqual(TEXT("提取 2 个参数"), CapturedArgs.Num(), 2);
    if (CapturedArgs.Num() == 2) {
        TestEqual(TEXT("双引号包裹的含空格参数完整提取"), CapturedArgs[0], FString(TEXT("Excalibur Blade")));
        TestEqual(TEXT("第二个参数解析正确"), CapturedArgs[1], FString(TEXT("5")));
    }

    TestTrue(
        TEXT("含转义双引号与反斜杠参数执行成功"),
        CheatRegistry.ExecuteCheat(TEXT("UnitTest.GM.GiveItem \"Hero \\\"Dragon\\\" Sword\" \"C:\\\\Loot\\\\Drop\""))
    );
    TestEqual(TEXT("转义测试提取 2 个参数"), CapturedArgs.Num(), 2);
    if (CapturedArgs.Num() == 2) {
        TestEqual(TEXT("转义双引号还原正确"), CapturedArgs[0], FString(TEXT("Hero \"Dragon\" Sword")));
        TestEqual(TEXT("转义反斜杠还原正确"), CapturedArgs[1], FString(TEXT("C:\\Loot\\Drop")));
    }

    TestTrue(
        TEXT("空双引号参数 \"\" 保留为独立空串参数"),
        CheatRegistry.ExecuteCheat(TEXT("  UnitTest.GM.GiveItem \"\" second \"\" "))
    );
    TestEqual(TEXT("空引号测试提取 3 个参数"), CapturedArgs.Num(), 3);
    if (CapturedArgs.Num() == 3) {
        TestEqual(TEXT("参数 0 为空字符串"), CapturedArgs[0], FString());
        TestEqual(TEXT("参数 1 为 second"), CapturedArgs[1], FString(TEXT("second")));
        TestEqual(TEXT("参数 2 为空字符串"), CapturedArgs[2], FString());
    }

    IConsoleObject* ConsoleObj = IConsoleManager::Get().FindConsoleObject(*GiveItemCmd.ToString());
    TestNotNull(TEXT("控制台命令指针非空"), ConsoleObj);
    if (ConsoleObj != nullptr && ConsoleObj->AsCommand() != nullptr) {
        TArray<FString> ConsoleArgs = {TEXT("ShieldOfLight"), TEXT("2")};
        ConsoleObj->AsCommand()->Execute(ConsoleArgs, nullptr, *GLog);
        TestEqual(TEXT("通过引擎控制台触发参数数量一致"), CapturedArgs.Num(), 2);
        if (CapturedArgs.Num() == 2) {
            TestEqual(TEXT("通过引擎 IConsoleCommand 直接触发回调生效"), CapturedArgs[0], FString(TEXT("ShieldOfLight")));
            TestEqual(TEXT("通过引擎控制台传递数量一致"), CapturedArgs[1], FString(TEXT("2")));
        }
    }

    IConsoleObject* PreExistingCmd = IConsoleManager::Get().RegisterConsoleCommand(
        *ConflictCmd.ToString(),
        TEXT("External pre-existing console command"),
        FConsoleCommandDelegate::CreateLambda([]() {})
    );
    {
        Game::Debug::Test::FScopedDebugLogCapture ConflictLog(TEXT("引擎控制台已存在同名对象"));
        int32 ConflictInvocations = 0;
        TestTrue(
            TEXT("引擎控制台存在同名对象时仍可注册到 ExecuteCheat"),
            CheatRegistry.RegisterCheat(ConflictCmd, TEXT("Shadow"), [&ConflictInvocations](const TArray<FString>&) {
                ++ConflictInvocations;
            })
        );
        TestEqual(TEXT("同名控制台冲突触发 LogGameDebug Warning"), ConflictLog.GetTotalCount(), 1);
        TestEqual(TEXT("冲突告警分类为 LogGameDebug"), ConflictLog.GetLastCategory(), FName(TEXT("LogGameDebug")));
        TestTrue(TEXT("通过 ExecuteCheat 仍可触发回调"), CheatRegistry.ExecuteCheat(ConflictCmd.ToString()));
        TestEqual(TEXT("冲突指令 ExecuteCheat 命中 1 次"), ConflictInvocations, 1);
        TestTrue(TEXT("注销冲突指令不误删外部既有控制台命令"), CheatRegistry.UnregisterCheat(ConflictCmd));
        TestNotNull(
            TEXT("外部既有控制台对象依然完好"),
            IConsoleManager::Get().FindConsoleObject(*ConflictCmd.ToString())
        );
    }
    if (PreExistingCmd != nullptr) {
        IConsoleManager::Get().UnregisterConsoleObject(PreExistingCmd, false);
    }

    TestFalse(TEXT("未知 GM 指令执行返回 false"), CheatRegistry.ExecuteCheat(TEXT("UnitTest.GM.NeverRegisteredXYZ 1")));
    TestFalse(TEXT("空白命令行执行返回 false"), CheatRegistry.ExecuteCheat(TEXT("   \t  ")));

    int32 OneShotInvocations = 0;
    TestTrue(
        TEXT("注册自注销一次性指令成功"),
        CheatRegistry.RegisterCheat(
            OneShotCmd,
            TEXT("Self-unregistering cheat to verify lock-free callback execution"),
            [&CheatRegistry, OneShotCmd, &OneShotInvocations](const TArray<FString>&) {
                ++OneShotInvocations;
                CheatRegistry.UnregisterCheat(OneShotCmd);
            }
        )
    );
    TestTrue(TEXT("回调内自注销指令执行无死锁"), CheatRegistry.ExecuteCheat(OneShotCmd.ToString()));
    TestEqual(TEXT("一次性指令执行计数为 1"), OneShotInvocations, 1);
    TestFalse(TEXT("自注销后指令已移除"), CheatRegistry.HasCheat(OneShotCmd));

    TestTrue(TEXT("手动注销已存在指令返回 true"), CheatRegistry.UnregisterCheat(GiveItemCmd));
    TestFalse(TEXT("重复注销返回 false"), CheatRegistry.UnregisterCheat(GiveItemCmd));
    TestNull(
        TEXT("注销后 IConsoleManager 中对应控制台对象已卸载"),
        IConsoleManager::Get().FindConsoleObject(*GiveItemCmd.ToString())
    );
    TestEqual(TEXT("清理后注册计数恢复基线"), CheatRegistry.GetCheatCount(), BaseCount);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCheatRegistryConcurrencyTest,
    "Game.Debug.Cheat.Concurrency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多线程并发执行、查询、注册与注销 GM 指令时的读写锁与控制台解耦安全性
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameCheatRegistryConcurrencyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    if constexpr (!Game::Config::bEnableGM) {
        return true;
    }

    FGameCheatRegistry& CheatRegistry = FGameCheatRegistry::Get();
    const int32 BaseCount = CheatRegistry.GetCheatCount();
    const FName SharedCmd(TEXT("UnitTest.GM.ConcurrentExec"));
    CheatRegistry.UnregisterCheat(SharedCmd);

    std::atomic<int32> SharedExecutions{0};
    TestTrue(
        TEXT("注册共享并发压测指令成功"),
        CheatRegistry.RegisterCheat(
            SharedCmd,
            TEXT("Concurrent execution target"),
            [&SharedExecutions](const TArray<FString>& Args) {
                if (Args.Num() == 2) {
                    SharedExecutions.fetch_add(1, std::memory_order_relaxed);
                }
            }
        )
    );

    constexpr int32 WorkerCount = 64;
    std::atomic<int32> DynamicRegisterSuccess{0};
    std::atomic<int32> DynamicUnregisterSuccess{0};

    ParallelFor(WorkerCount, [&](int32 WorkerIdx) {
        const FName DynamicCmd(*FString::Printf(TEXT("UnitTest.GM.WorkerCmd_%d"), WorkerIdx));
        if (CheatRegistry.RegisterCheat(DynamicCmd, TEXT("Worker cmd"), [](const TArray<FString>&) {})) {
            DynamicRegisterSuccess.fetch_add(1, std::memory_order_relaxed);
        }

        for (int32 Iter = 0; Iter < 32; ++Iter) {
            (void)CheatRegistry.HasCheat(SharedCmd);
            (void)CheatRegistry.GetCheatCount();
            (void)CheatRegistry.ExecuteCheat(TEXT("UnitTest.GM.ConcurrentExec \"Arg One\" 42"));
        }

        if (CheatRegistry.UnregisterCheat(DynamicCmd)) {
            DynamicUnregisterSuccess.fetch_add(1, std::memory_order_relaxed);
        }
    });

    TestEqual(TEXT("全部工作线程动态指令注册成功"), DynamicRegisterSuccess.load(), WorkerCount);
    TestEqual(TEXT("全部工作线程动态指令注销成功"), DynamicUnregisterSuccess.load(), WorkerCount);
    TestEqual(TEXT("多线程共享执行总次数精确匹配"), SharedExecutions.load(), WorkerCount * 32);

    const FName ContendedCmd(TEXT("UnitTest.GM.ContendedABA"));
    CheatRegistry.UnregisterCheat(ContendedCmd);
    {
        Game::Debug::Test::FScopedDebugLogCapture FalseConflictLog(TEXT("引擎控制台已存在同名对象"));
        ParallelFor(32, [&](int32) {
            for (int32 Iter = 0; Iter < 16; ++Iter) {
                (void)CheatRegistry.RegisterCheat(ContendedCmd, TEXT("ABA cmd"), [](const TArray<FString>&) {});
                (void)CheatRegistry.ExecuteCheat(TEXT("UnitTest.GM.ContendedABA 1"));
                (void)CheatRegistry.UnregisterCheat(ContendedCmd);
            }
        });
        TestEqual(TEXT("同名高频并发注册/注销零误报引擎控制台冲突"), FalseConflictLog.GetTotalCount(), 0);
    }
    CheatRegistry.UnregisterCheat(ContendedCmd);
    TestNull(
        TEXT("同名并发压力结束后引擎控制台无残留句柄"),
        IConsoleManager::Get().FindConsoleObject(*ContendedCmd.ToString())
    );

    TestTrue(TEXT("注销共享并发压测指令成功"), CheatRegistry.UnregisterCheat(SharedCmd));
    TestEqual(TEXT("并发测试结束后指令总数回归基线"), CheatRegistry.GetCheatCount(), BaseCount);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
