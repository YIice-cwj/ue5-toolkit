/**
 * @file FGameDebugTimeControllerTest.cpp
 * @brief FGameDebugTimeController 时间膨胀、暂停记忆、确定性单步重暂停、GM 交互与卸载生命周期真测试
 */

#include "Time/FGameDebugTimeController.h"

#include "Cheat/FGameCheatRegistry.h"
#include "Config/GameCompileSwitches.h"
#include "Tests/GameDebugTestUtils.h"

#if WITH_AUTOMATION_TESTS

#include "Containers/Ticker.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugTimeControllerTest,
    "Game.Debug.Time.GlobalDilationAndGM",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证时间倍率边界与 0.001f 非暂停契约、NaN/无效上下文拦截、确定性 StepFrame 自动重暂停与取消、GM 正反指令及 Shutdown/Initialize 对称恢复
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameDebugTimeControllerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    if constexpr (!Game::Config::bEnableGM && !Game::Config::bEnableDebugDraw) {
        return true;
    }

    UWorld* World = Game::Debug::Test::GetAutomationTestWorld();
    TestNotNull(TEXT("自动化测试环境存在有效 UWorld"), World);
    if (World == nullptr) {
        return false;
    }

    FGameDebugTimeController::Initialize();
    Game::Debug::Test::FScopedScreenMessageGuard ScreenGuard;

    TestTrue(TEXT("重置初始世界时钟成功"), FGameDebugTimeController::ResetTime(World));
    TestEqual(TEXT("重置后时间倍率为 1.0f"), FGameDebugTimeController::GetTimeDilation(World), 1.0f);
    TestFalse(TEXT("1.0f 不处于暂停态"), FGameDebugTimeController::IsPaused(World));

    TestTrue(TEXT("设置合法最小边界 0.001f 成功"), FGameDebugTimeController::SetTimeDilation(World, FGameDebugTimeController::MinTimeDilation));
    TestEqual(TEXT("读取当前倍率为 0.001f"), FGameDebugTimeController::GetTimeDilation(World), FGameDebugTimeController::MinTimeDilation);
    TestFalse(TEXT("合法下界 0.001f 不应被误判为暂停"), FGameDebugTimeController::IsPaused(World));

    TestTrue(TEXT("超过上限 99.0f 钳制至 20.0f"), FGameDebugTimeController::SetTimeDilation(World, 99.0f));
    TestEqual(TEXT("上限钳制生效为 20.0f"), FGameDebugTimeController::GetTimeDilation(World), FGameDebugTimeController::MaxTimeDilation);

    TestTrue(TEXT("低于下限 -3.0f 钳制至 0.001f"), FGameDebugTimeController::SetTimeDilation(World, -3.0f));
    TestEqual(TEXT("下限钳制生效为 0.001f"), FGameDebugTimeController::GetTimeDilation(World), FGameDebugTimeController::MinTimeDilation);

    {
        Game::Debug::Test::FScopedDebugLogCapture NaNLog(TEXT("拒绝设置非有限时间倍率"));
        TestFalse(
            TEXT("NaN 时间倍率被拦截"),
            FGameDebugTimeController::SetTimeDilation(World, std::numeric_limits<float>::quiet_NaN())
        );
        TestFalse(
            TEXT("Inf 时间倍率被拦截"),
            FGameDebugTimeController::SetTimeDilation(World, std::numeric_limits<float>::infinity())
        );
        TestEqual(TEXT("两次非有限倍率均触发 LogGameDebug Warning"), NaNLog.GetTotalCount(), 2);
        TestEqual(TEXT("告警分类为 LogGameDebug"), NaNLog.GetLastCategory(), FName(TEXT("LogGameDebug")));
    }

    const UObject* WorldlessObj = GetTransientPackage();
    TestTrue(TEXT("先设置目标世界为 0.5x"), FGameDebugTimeController::SetTimeDilation(World, 0.5f));
    TestFalse(
        TEXT("显式传入无世界的非空上下文时拒绝穿透污染其他世界"),
        FGameDebugTimeController::SetTimeDilation(WorldlessObj, 2.0f)
    );
    TestEqual(TEXT("原世界倍率保持 0.5x 未被误改"), FGameDebugTimeController::GetTimeDilation(World), 0.5f);

    TestTrue(TEXT("设置 0.25x 慢动作成功"), FGameDebugTimeController::SetTimeDilation(World, 0.25f));
    TestTrue(TEXT("暂停世界时钟成功"), FGameDebugTimeController::Pause(World));
    TestTrue(TEXT("暂停后 IsPaused 返回 true"), FGameDebugTimeController::IsPaused(World));
    TestEqual(TEXT("暂停后世界倍率为 PausedTimeDilation"), FGameDebugTimeController::GetTimeDilation(World), FGameDebugTimeController::PausedTimeDilation);

    TestTrue(TEXT("StepFrame 3 帧启动成功"), FGameDebugTimeController::StepFrame(World, 3));
    TestEqual(TEXT("步进期间恢复至记忆倍率 0.25x"), FGameDebugTimeController::GetTimeDilation(World), 0.25f);
    TestFalse(TEXT("步进期间不处于暂停态"), FGameDebugTimeController::IsPaused(World));

    FTSTicker::GetCoreTicker().Tick(0.016f);
    TestFalse(TEXT("步进第 1 帧后仍保持运行"), FGameDebugTimeController::IsPaused(World));
    FTSTicker::GetCoreTicker().Tick(0.016f);
    TestFalse(TEXT("步进第 2 帧后仍保持运行"), FGameDebugTimeController::IsPaused(World));
    FTSTicker::GetCoreTicker().Tick(0.016f);
    TestTrue(TEXT("步进第 3 帧后自动重新暂停冻结"), FGameDebugTimeController::IsPaused(World));

    const uint8 PrevInTick = World->bInTick;
    World->bInTick = true;
    TestTrue(TEXT("在 UWorld::Tick 中途调用 StepFrame(1) 自动补偿同帧 CoreTicker"), FGameDebugTimeController::StepFrame(World, 1));
    World->bInTick = PrevInTick;
    FTSTicker::GetCoreTicker().Tick(0.016f);
    TestFalse(TEXT("同帧尾部 CoreTicker 不应过早重暂停，保证完整步进下一帧"), FGameDebugTimeController::IsPaused(World));
    FTSTicker::GetCoreTicker().Tick(0.016f);
    TestTrue(TEXT("下一完整帧结束后自动重暂停"), FGameDebugTimeController::IsPaused(World));

    TestTrue(TEXT("再次启动 StepFrame 2 帧"), FGameDebugTimeController::StepFrame(World, 2));
    TestTrue(TEXT("中途调用 SetTimeDilation 取消待执行步进"), FGameDebugTimeController::SetTimeDilation(World, 0.75f));
    FTSTicker::GetCoreTicker().Tick(0.016f);
    FTSTicker::GetCoreTicker().Tick(0.016f);
    TestFalse(TEXT("被取消的步进 Ticker 不再误暂停世界"), FGameDebugTimeController::IsPaused(World));
    TestEqual(TEXT("世界倍率稳定保持 0.75x"), FGameDebugTimeController::GetTimeDilation(World), 0.75f);

    {
        const uint8 PrevOverflowInTick = World->bInTick;
        World->bInTick = true;
        TestTrue(TEXT("StepFrame(MAX_int32) 在 bInTick=true 下无整数溢出"), FGameDebugTimeController::StepFrame(World, MAX_int32));
        World->bInTick = PrevOverflowInTick;
        FTSTicker::GetCoreTicker().Tick(0.016f);
        TestFalse(TEXT("MAX_int32 步进不会因溢出提前暂停"), FGameDebugTimeController::IsPaused(World));
        TestTrue(TEXT("重置取消 MAX_int32 步进 Ticker"), FGameDebugTimeController::ResetTime(World));
    }

    if (AWorldSettings* Settings = World->GetWorldSettings()) {
        const float PrevMinDilation = Settings->MinGlobalTimeDilation;
        Settings->MinGlobalTimeDilation = 0.05f;
        Game::Debug::Test::FScopedDebugLogCapture MinClampLog(TEXT("MinGlobalTimeDilation"));
        TestFalse(TEXT("WorldSettings 下限高于暂停阈值时 Pause 返回 false"), FGameDebugTimeController::Pause(World));
        TestEqual(TEXT("暂停受限时触发 LogGameDebug Warning"), MinClampLog.GetTotalCount(), 1);
        Settings->MinGlobalTimeDilation = PrevMinDilation;
        TestTrue(TEXT("恢复 MinGlobalTimeDilation 后重设 0.75x"), FGameDebugTimeController::SetTimeDilation(World, 0.75f));
    }

    TestTrue(TEXT("暂停后 Resume 恢复至记忆倍率 0.75x"), FGameDebugTimeController::Pause(World));
    TestTrue(TEXT("Resume 成功"), FGameDebugTimeController::Resume(World));
    TestEqual(TEXT("恢复后时间倍率为 0.75x"), FGameDebugTimeController::GetTimeDilation(World), 0.75f);

    if constexpr (Game::Config::bEnableGM) {
        FGameCheatRegistry& CheatRegistry = FGameCheatRegistry::Get();
        TestTrue(TEXT("GM.TimeScale 已注册"), CheatRegistry.HasCheat(FName(TEXT("GM.TimeScale"))));
        TestTrue(TEXT("GM.TimeScale 0.1 执行成功"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeScale 0.1")));
        TestEqual(TEXT("GM.TimeScale 设置 0.1x 生效"), FGameDebugTimeController::GetTimeDilation(World), 0.1f);

        {
            Game::Debug::Test::FScopedDebugLogCapture BadArgLog(TEXT("收到非法"));
            TestTrue(TEXT("GM.TimeScale 缺参安全返回并提示用法"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeScale")));
            TestTrue(TEXT("GM.TimeScale 非法字符串被拦截"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeScale not_a_number")));
            TestTrue(TEXT("GM.TimeScale 尾随杂字符被严格拦截"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeScale 0.5abc")));
            TestTrue(TEXT("GM.TimeScale 孤立符号被拦截"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeScale +")));
            TestTrue(TEXT("GM.TimeStep 非法帧数被拦截"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeStep invalid")));
            TestTrue(TEXT("GM.TimeStep 尾随杂字符被严格拦截"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeStep 3abc")));
            TestTrue(TEXT("GM.TimeStep 浮点数被严格拦截"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeStep 1.5")));
            TestTrue(TEXT("GM.TimeStep 零帧被拦截"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeStep 0")));
            TestTrue(TEXT("GM.TimeStep 负帧被拦截"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeStep -5")));
            TestEqual(TEXT("全部非法 GM 参数保留原倍率 0.1x 不变"), FGameDebugTimeController::GetTimeDilation(World), 0.1f);
            TestEqual(TEXT("8 次非法参数均触发 LogGameDebug Warning"), BadArgLog.GetTotalCount(), 8);
        }

        TestTrue(TEXT("GM.TimePause 执行成功"), CheatRegistry.ExecuteCheat(TEXT("GM.TimePause")));
        TestTrue(TEXT("GM.TimePause 生效"), FGameDebugTimeController::IsPaused(World));

        TestTrue(TEXT("GM.TimeResume 执行成功"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeResume")));
        TestEqual(TEXT("GM.TimeResume 恢复记忆倍率 0.1x"), FGameDebugTimeController::GetTimeDilation(World), 0.1f);

        TestTrue(TEXT("GM.TimeReset 执行成功"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeReset")));
        TestEqual(TEXT("GM.TimeReset 重置为 1.0x"), FGameDebugTimeController::GetTimeDilation(World), 1.0f);

        TestTrue(TEXT("GM.TimeStep 1 执行成功"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeStep 1")));
        FTSTicker::GetCoreTicker().Tick(0.016f);
        TestTrue(TEXT("GM.TimeStep 1 步进 1 帧后自动重暂停"), FGameDebugTimeController::IsPaused(World));

        TestTrue(TEXT("启动待执行步进后测试 Shutdown 清理"), FGameDebugTimeController::StepFrame(World, 2));
        FGameDebugTimeController::Shutdown();
        TestFalse(TEXT("Shutdown 后 GM.TimeScale 已注销"), CheatRegistry.HasCheat(FName(TEXT("GM.TimeScale"))));
        FTSTicker::GetCoreTicker().Tick(0.016f);
        FTSTicker::GetCoreTicker().Tick(0.016f);
        TestFalse(TEXT("Shutdown 已注销待执行步进 Ticker，不再误暂停"), FGameDebugTimeController::IsPaused(World));

        FGameDebugTimeController::Initialize();
        TestTrue(TEXT("重新 Initialize 后 GM.TimeScale 恢复可用"), CheatRegistry.HasCheat(FName(TEXT("GM.TimeScale"))));
    }

    TestTrue(TEXT("测试收尾重置世界时钟为 1.0x"), FGameDebugTimeController::ResetTime(World));
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
