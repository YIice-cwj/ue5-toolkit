/**
 * @file FGameCheatRegistryTest.cpp
 * @brief FGameCheatRegistry 模块化调试/GM 指令注册中心自动化测试
 */

#include "Cheat/FGameCheatRegistry.h"

#include "Config/GameCompileSwitches.h"

#if WITH_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCheatRegistryLifecycleTest,
    "Game.Debug.Cheat.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 GM 指令注册、重名拦截、带引号参数解析、IConsoleManager 联动、回调内重入安全与注销清理
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
    CheatRegistry.UnregisterCheat(GiveItemCmd);
    CheatRegistry.UnregisterCheat(OneShotCmd);

    const int32 BaseCount = CheatRegistry.GetCheatCount();

    TestFalse(
        TEXT("空名称注册被拒绝"),
        CheatRegistry.RegisterCheat(NAME_None, TEXT("Invalid"), [](const TArray<FString>&) {})
    );
    TestFalse(
        TEXT("空回调注册被拒绝"),
        CheatRegistry.RegisterCheat(GiveItemCmd, TEXT("Invalid"), FGameCheatRegistry::FCheatHandler())
    );

    FString LastItemName;
    int32 LastItemCount = 0;
    const bool bRegistered = CheatRegistry.RegisterCheat(
        GiveItemCmd,
        TEXT("Grant test item by name and count"),
        [&LastItemName, &LastItemCount](const TArray<FString>& Args) {
            if (Args.Num() >= 1) {
                LastItemName = Args[0];
            }
            if (Args.Num() >= 2) {
                LexTryParseString(LastItemCount, *Args[1]);
            }
        }
    );

    TestTrue(TEXT("GM 指令首次注册成功"), bRegistered);
    TestTrue(TEXT("HasCheat 查询为 true"), CheatRegistry.HasCheat(GiveItemCmd));
    TestEqual(TEXT("注册计数递增 1"), CheatRegistry.GetCheatCount(), BaseCount + 1);
    TestNotNull(
        TEXT("IConsoleManager 已挂载同名控制台对象"),
        IConsoleManager::Get().FindConsoleObject(*GiveItemCmd.ToString())
    );

    TestFalse(
        TEXT("同名 GM 指令重复注册被拦截"),
        CheatRegistry.RegisterCheat(GiveItemCmd, TEXT("Dup"), [](const TArray<FString>&) {})
    );

    const bool bExecuted = CheatRegistry.ExecuteCheat(TEXT("UnitTest.GM.GiveItem \"Excalibur Blade\" 5"));
    TestTrue(TEXT("ExecuteCheat 命中并执行成功"), bExecuted);
    TestEqual(TEXT("双引号包裹的含空格参数完整提取"), LastItemName, FString(TEXT("Excalibur Blade")));
    TestEqual(TEXT("第二个整型参数解析正确"), LastItemCount, 5);

    IConsoleObject* ConsoleObj = IConsoleManager::Get().FindConsoleObject(*GiveItemCmd.ToString());
    if (ConsoleObj != nullptr && ConsoleObj->AsCommand() != nullptr) {
        TArray<FString> ConsoleArgs;
        ConsoleArgs.Add(TEXT("ShieldOfLight"));
        ConsoleArgs.Add(TEXT("2"));
        ConsoleObj->AsCommand()->Execute(ConsoleArgs, nullptr, *GLog);
        TestEqual(TEXT("通过引擎 IConsoleCommand 直接触发回调生效"), LastItemName, FString(TEXT("ShieldOfLight")));
        TestEqual(TEXT("通过引擎控制台传递数量一致"), LastItemCount, 2);
    }

    TestFalse(TEXT("未知 GM 指令执行返回 false"), CheatRegistry.ExecuteCheat(TEXT("UnitTest.GM.NonExistent 1")));
    TestFalse(TEXT("空白命令行执行返回 false"), CheatRegistry.ExecuteCheat(TEXT("   ")));

    int32 OneShotInvocations = 0;
    CheatRegistry.RegisterCheat(
        OneShotCmd,
        TEXT("Self-unregistering cheat to verify lock-free callback execution"),
        [&CheatRegistry, OneShotCmd, &OneShotInvocations](const TArray<FString>&) {
            ++OneShotInvocations;
            CheatRegistry.UnregisterCheat(OneShotCmd);
        }
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

#endif  // WITH_AUTOMATION_TESTS
