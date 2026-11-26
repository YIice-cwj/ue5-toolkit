/**
 * @file FGameDebugWatchHubTest.cpp
 * @brief FGameDebugWatchHub 调试观察中心真测试（渲染输出验证、通道隔离、即时撤销、重入安全、GM 指令、生命周期与并发）
 */

#include "Watcher/FGameDebugWatchHub.h"

#include "Cheat/FGameCheatRegistry.h"
#include "Config/GameCompileSwitches.h"
#include "Core/GameDebugScreenKeys.h"
#include "Print/FGameDebugPrint.h"
#include "Tests/GameDebugTestUtils.h"

#if WITH_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugWatchHubLifecycleTest,
    "Game.Debug.WatchHub.LifecycleAndCommands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证观察中心注册/覆盖/空分类回收、即时屏幕面板撤销、与 PrintChannel 正交隔离、回调重入安全、GM.Watch 全语法与对称启停
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameDebugWatchHubLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return true;
    }

    TestNotNull(TEXT("自动化测试环境 GEngine 有效"), GEngine);
    if (GEngine == nullptr) {
        return false;
    }

    FGameDebugWatchHub& Hub = FGameDebugWatchHub::Get();
    Hub.Initialize();
    Hub.ClearAll();
    Hub.SetGlobalEnabled(true);

    Game::Debug::Test::FScopedPrintCapture PrintCapture;
    Game::Debug::Test::FCapturedPrintRecord PrintRecord;
    Game::Debug::Test::FCapturedPrintRecord WatchRecord;

    const FName CatPlayer(TEXT("Player"));
    const FName CatTasks(TEXT("Tasks"));
    const FName NameSpeed(TEXT("Speed"));
    const FName NameHp(TEXT("HP"));

    TestFalse(TEXT("空分类注册被拒绝"), Hub.Watch(NAME_None, NameSpeed, []() { return TEXT("0"); }));
    TestFalse(TEXT("空名称注册被拒绝"), Hub.Watch(CatPlayer, NAME_None, []() { return TEXT("0"); }));
    TestFalse(TEXT("空回调注册被拒绝"), Hub.Watch(CatPlayer, NameSpeed, FGameDebugWatchHub::FValueGetter()));

    int32 SpeedValue = 100;
    TestTrue(TEXT("注册 Player.Speed 成功"), Hub.Watch(CatPlayer, NameSpeed, [&SpeedValue]() {
        return FString::FromInt(SpeedValue);
    }));
    TestTrue(TEXT("注册 Player.HP 成功"), Hub.Watch(CatPlayer, NameHp, []() {
        return TEXT("100/100");
    }));
    TestTrue(TEXT("注册 Tasks.Pending 成功"), Hub.Watch(CatTasks, TEXT("Pending"), []() {
        return TEXT("3");
    }));

    TestTrue(TEXT("Player 分组默认开启"), Hub.IsCategoryEnabled(CatPlayer));
    TestTrue(TEXT("Tasks 分组默认开启"), Hub.IsCategoryEnabled(CatTasks));

    TArray<FName> Categories;
    Hub.GetCategories(Categories);
    TestEqual(TEXT("注册了 2 个分类"), Categories.Num(), 2);
    TestEqual(TEXT("Player 分类下有 2 个观察项"), Hub.GetWatchItemCount(CatPlayer), 2);
    TestEqual(TEXT("Tasks 分类下有 1 个观察项"), Hub.GetWatchItemCount(CatTasks), 1);
    TestEqual(TEXT("不存在分类返回 0"), Hub.GetWatchItemCount(FName(TEXT("NonExistent"))), 0);

    FGameDebugPrint::PrintChannel(CatPlayer, TEXT("Player Print Channel Payload"), FColor::Yellow, 2.0f);
    Hub.TickDisplay(0.2f);

    const uint64 PrintPlayerKey = FGameDebugPrint::GetChannelKey(CatPlayer);
    const uint64 WatchPlayerKey = Game::Debug::Private::MakeWatchHubCategoryKey(CatPlayer);
    const uint64 WatchTasksKey = Game::Debug::Private::MakeWatchHubCategoryKey(CatTasks);

    TestTrue(TEXT("UEngine 同时保留 PrintChannel(Player) 槽位"), Game::Debug::Test::HasKeyedScreenMessage(PrintPlayerKey));
    TestTrue(TEXT("UEngine 同时保留 WatchHub(Player) 槽位"), Game::Debug::Test::HasKeyedScreenMessage(WatchPlayerKey));

    TestTrue(TEXT("PrintChannel(Player) 记录存在"), PrintCapture.FindLastByKey(PrintPlayerKey, PrintRecord));
    TestTrue(TEXT("WatchHub(Player) 记录存在"), PrintCapture.FindLastByKey(WatchPlayerKey, WatchRecord));
    TestEqual(TEXT("PrintChannel 正文完整保留"), PrintRecord.Message, FString(TEXT("[Player] Player Print Channel Payload")));
    TestTrue(TEXT("WatchHub 正文包含分类头"), WatchRecord.Message.Contains(TEXT("=== [ Player ] ===")));
    TestTrue(TEXT("WatchHub 正文包含 Speed 采样值 100"), WatchRecord.Message.Contains(TEXT("100")));
    TestTrue(TEXT("WatchHub 驻留时长自适应低帧 DeltaTime (0.2*2=0.4s)"), FMath::IsNearlyEqual(WatchRecord.Duration, 0.4f, 0.001f));

    SpeedValue = 250;
    TestTrue(TEXT("覆盖更新 Player.Speed Getter 成功"), Hub.Watch(CatPlayer, NameSpeed, [&SpeedValue]() {
        return FString::Printf(TEXT("%d km/h"), SpeedValue);
    }));
    TestEqual(TEXT("覆盖更新不增加观察项计数"), Hub.GetWatchItemCount(CatPlayer), 2);

    Hub.TickDisplay(0.016f);
    TestTrue(TEXT("覆盖更新后重新采样成功"), PrintCapture.FindLastByKey(WatchPlayerKey, WatchRecord));
    TestTrue(TEXT("屏幕输出刷新为最新 Getter 结果 250 km/h"), WatchRecord.Message.Contains(TEXT("250 km/h")));

    const FName CatReentrant(TEXT("Reentrant"));
    bool bReentrantExecuted = false;
    TestTrue(
        TEXT("注册会在采样回调内反向修改 Hub 的重入项"),
        Hub.Watch(CatReentrant, TEXT("Mutator"), [&Hub, CatReentrant, &bReentrantExecuted]() {
            bReentrantExecuted = true;
            Hub.Unwatch(CatReentrant, TEXT("Mutator"));
            return TEXT("Done");
        })
    );
    Hub.TickDisplay(0.016f);
    TestTrue(TEXT("锁外快照派发重入回调无死锁且正常执行"), bReentrantExecuted);
    TestEqual(TEXT("重入自注销后空分类自动回收"), Hub.GetWatchItemCount(CatReentrant), 0);
    TestFalse(TEXT("默认开启态的空分类已从分类表清理"), Hub.IsCategoryEnabled(CatReentrant));

    Hub.SetCategoryEnabled(CatPlayer, false);
    TestFalse(TEXT("Player 分组成功关闭"), Hub.IsCategoryEnabled(CatPlayer));
    TestFalse(TEXT("关闭分类后立即从引擎屏幕撤销对应面板（无需等待超时）"), Game::Debug::Test::HasKeyedScreenMessage(WatchPlayerKey));
    TestTrue(TEXT("撤销 WatchHub(Player) 不误伤同名 PrintChannel(Player)"), Game::Debug::Test::HasKeyedScreenMessage(PrintPlayerKey));
    TestTrue(TEXT("开启态的 Tasks 面板依然保留在屏幕"), Game::Debug::Test::HasKeyedScreenMessage(WatchTasksKey));

    Hub.SetCategoryEnabled(CatPlayer, true);
    TestTrue(TEXT("Player 分组重新开启"), Hub.IsCategoryEnabled(CatPlayer));
    Hub.TickDisplay(0.016f);
    TestTrue(TEXT("重新开启后 TickDisplay 恢复 Player 面板"), Game::Debug::Test::HasKeyedScreenMessage(WatchPlayerKey));

    if constexpr (Game::Config::bEnableGM) {
        FGameCheatRegistry& CheatRegistry = FGameCheatRegistry::Get();
        TestTrue(TEXT("GM.Watch 指令已注册"), CheatRegistry.HasCheat(FName(TEXT("GM.Watch"))));
        TestTrue(TEXT("执行 GM.Watch list 成功"), CheatRegistry.ExecuteCheat(TEXT("GM.Watch list")));

        TestTrue(TEXT("执行 GM.Watch all off 成功"), CheatRegistry.ExecuteCheat(TEXT("GM.Watch all off")));
        TestFalse(TEXT("全局总开关已被关闭"), Hub.IsGlobalEnabled());
        TestFalse(TEXT("全局关闭后立即撤销全部观察面板"), Game::Debug::Test::HasKeyedScreenMessage(WatchPlayerKey));
        TestFalse(TEXT("全局关闭后 Tasks 面板同步撤销"), Game::Debug::Test::HasKeyedScreenMessage(WatchTasksKey));
        PrintCapture.Reset();
        Hub.TickDisplay(0.016f);
        TestEqual(TEXT("全局关闭时 TickDisplay 零输出"), PrintCapture.Num(), 0);

        TestTrue(TEXT("执行 GM.Watch all true 成功"), CheatRegistry.ExecuteCheat(TEXT("GM.Watch all true")));
        TestTrue(TEXT("全局总开关已被重新开启"), Hub.IsGlobalEnabled());

        TestTrue(TEXT("执行 GM.Watch Tasks 0 成功"), CheatRegistry.ExecuteCheat(TEXT("GM.Watch Tasks 0")));
        TestFalse(TEXT("Tasks 分组已被关闭"), Hub.IsCategoryEnabled(CatTasks));

        {
            Game::Debug::Test::FScopedDebugLogCapture BadToggleLog(TEXT("非法开关参数"));
            TestTrue(TEXT("GM.Watch 缺参安全返回提示"), CheatRegistry.ExecuteCheat(TEXT("GM.Watch")));
            TestTrue(TEXT("GM.Watch all 缺参安全返回提示"), CheatRegistry.ExecuteCheat(TEXT("GM.Watch all")));
            TestTrue(TEXT("GM.Watch 非法参数被拦截"), CheatRegistry.ExecuteCheat(TEXT("GM.Watch Player typo_flag")));
            TestTrue(TEXT("非法参数不改变 Player 原开启状态"), Hub.IsCategoryEnabled(CatPlayer));
            TestEqual(TEXT("非法开关参数通过 LogGameDebug 记录 Warning"), BadToggleLog.GetTotalCount(), 1);
            TestEqual(TEXT("告警分类为 LogGameDebug"), BadToggleLog.GetLastCategory(), FName(TEXT("LogGameDebug")));
        }

        {
            Game::Debug::Test::FScopedDebugLogCapture EmptyCatLog(TEXT("空分类名称"));
            TestTrue(TEXT("GM.Watch 空引号分类被安全拦截"), CheatRegistry.ExecuteCheat(TEXT("GM.Watch \"\" off")));
            TestEqual(TEXT("空分类名称触发 LogGameDebug Warning"), EmptyCatLog.GetTotalCount(), 1);
        }
    }

    TestTrue(TEXT("注销单项 Player.Speed 成功"), Hub.Unwatch(CatPlayer, NameSpeed));
    TestFalse(TEXT("重复注销返回 false"), Hub.Unwatch(CatPlayer, NameSpeed));
    TestTrue(TEXT("注销 Tasks 唯一项成功"), Hub.Unwatch(CatTasks, TEXT("Pending")));
    TestEqual(TEXT("已关闭的 Tasks 分类仍保留关闭偏好"), Hub.GetWatchItemCount(CatTasks), 0);

    Hub.Shutdown();
    if constexpr (Game::Config::bEnableGM) {
        TestFalse(TEXT("Shutdown 后 GM.Watch 已注销"), FGameCheatRegistry::Get().HasCheat(FName(TEXT("GM.Watch"))));
    }
    Hub.GetCategories(Categories);
    TestEqual(TEXT("Shutdown 后分类总数为 0"), Categories.Num(), 0);
    TestTrue(TEXT("Shutdown 后全局开关复位为 true"), Hub.IsGlobalEnabled());

    Hub.Initialize();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugWatchHubConcurrencyTest,
    "Game.Debug.WatchHub.Concurrency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多线程并发注册/注销/开关切换与全局原子总开关读写安全性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameDebugWatchHubConcurrencyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return true;
    }

    FGameDebugWatchHub& Hub = FGameDebugWatchHub::Get();
    Hub.ClearAll();
    Hub.SetGlobalEnabled(true);

    constexpr int32 WorkerCount = 32;
    ParallelFor(WorkerCount, [&](int32 WorkerIdx) {
        const FName CatName(*FString::Printf(TEXT("ConcurrentCat_%d"), WorkerIdx % 4));
        const FName ItemName(*FString::Printf(TEXT("Item_%d"), WorkerIdx));
        for (int32 Iter = 0; Iter < 20; ++Iter) {
            (void)Hub.Watch(CatName, ItemName, [WorkerIdx, Iter]() {
                return FString::Printf(TEXT("%d:%d"), WorkerIdx, Iter);
            });
            Hub.SetGlobalEnabled((Iter & 1) == 0);
            (void)Hub.IsGlobalEnabled();
            (void)Hub.GetWatchItemCount(CatName);
            (void)Hub.Unwatch(CatName, ItemName);
        }
    });

    Hub.SetGlobalEnabled(true);
    TArray<FName> RemainingCategories;
    Hub.GetCategories(RemainingCategories);
    TestEqual(TEXT("并发全部注销后空分类全部自动回收"), RemainingCategories.Num(), 0);
    Hub.ClearAll();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
