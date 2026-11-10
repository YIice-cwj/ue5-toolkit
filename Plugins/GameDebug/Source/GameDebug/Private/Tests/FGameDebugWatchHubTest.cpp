/**
 * @file FGameDebugWatchHubTest.cpp
 * @brief FGameDebugWatchHub 调试观察中心单元测试
 */

#include "Watcher/FGameDebugWatchHub.h"
#include "Cheat/FGameCheatRegistry.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugWatchHubLifecycleTest,
    "Game.Debug.WatchHub.LifecycleAndCommands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证调试观察中心的分组注册、注销、开关及控制台 GM 命令交互
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameDebugWatchHubLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameDebugWatchHub& Hub = FGameDebugWatchHub::Get();
    Hub.ClearAll();

    const FName CatPlayer(TEXT("Player"));
    const FName CatTasks(TEXT("Tasks"));
    const FName NameSpeed(TEXT("Speed"));
    const FName NameHp(TEXT("HP"));

    int32 SpeedValue = 100;
    Hub.Watch(CatPlayer, NameSpeed, [&SpeedValue]() {
        return FString::FromInt(SpeedValue);
    });
    Hub.Watch(CatPlayer, NameHp, []() {
        return TEXT("100/100");
    });
    Hub.Watch(CatTasks, TEXT("Pending"), []() {
        return TEXT("0");
    });

    TestTrue(TEXT("Player 分组默认开启"), Hub.IsCategoryEnabled(CatPlayer));
    TestTrue(TEXT("Tasks 分组默认开启"), Hub.IsCategoryEnabled(CatTasks));

    TArray<FName> Categories;
    Hub.GetCategories(Categories);
    TestEqual(TEXT("注册了 2 个分类"), Categories.Num(), 2);

    Hub.TickDisplay(0.016f);

    SpeedValue = 250;
    Hub.Watch(CatPlayer, NameSpeed, [&SpeedValue]() {
        return FString::FromInt(SpeedValue);
    });

    Hub.SetCategoryEnabled(CatPlayer, false);
    TestFalse(TEXT("Player 分组成功关闭"), Hub.IsCategoryEnabled(CatPlayer));

    Hub.SetCategoryEnabled(CatPlayer, true);
    TestTrue(TEXT("Player 分组重新开启"), Hub.IsCategoryEnabled(CatPlayer));

    FGameCheatRegistry& CheatRegistry = FGameCheatRegistry::Get();
    TestTrue(TEXT("GM.Watch 指令已注册"), CheatRegistry.HasCheat(FName(TEXT("GM.Watch"))));

    TestTrue(TEXT("执行 GM.Watch list 成功"), CheatRegistry.ExecuteCheat(TEXT("GM.Watch list")));
    TestTrue(TEXT("执行 GM.Watch all off 成功"), CheatRegistry.ExecuteCheat(TEXT("GM.Watch all off")));
    TestFalse(TEXT("全局总开关已被关闭"), Hub.IsGlobalEnabled());

    TestTrue(TEXT("执行 GM.Watch all on 成功"), CheatRegistry.ExecuteCheat(TEXT("GM.Watch all on")));
    TestTrue(TEXT("全局总开关已被重新开启"), Hub.IsGlobalEnabled());

    TestTrue(TEXT("执行 GM.Watch Tasks off 成功"), CheatRegistry.ExecuteCheat(TEXT("GM.Watch Tasks off")));
    TestFalse(TEXT("Tasks 分组已被关闭"), Hub.IsCategoryEnabled(CatTasks));

    TestTrue(TEXT("注销单项成功"), Hub.Unwatch(CatPlayer, NameSpeed));
    TestFalse(TEXT("重复注销返回 false"), Hub.Unwatch(CatPlayer, NameSpeed));

    Hub.ClearAll();
    Categories.Reset();
    Hub.GetCategories(Categories);
    TestEqual(TEXT("ClearAll 后分类总数为 0"), Categories.Num(), 0);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
