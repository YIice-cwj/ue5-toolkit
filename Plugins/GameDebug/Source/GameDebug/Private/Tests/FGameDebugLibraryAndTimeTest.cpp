/**
 * @file FGameDebugLibraryAndTimeTest.cpp
 * @brief 蓝图函数库、时间控制器、作用域秒表与运动轨迹记录器自动化测试
 */

#include "Config/GameCompileSwitches.h"
#include "Library/UGameDebugBlueprintLibrary.h"
#include "Profiling/FScopedDebugStopwatch.h"
#include "Time/FGameDebugTimeController.h"
#include "Visualizer/FGameDebugTrailRecorder.h"

#if WITH_AUTOMATION_TESTS

#include "Cheat/FGameCheatRegistry.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugBlueprintLibraryTest,
    "Game.Debug.Library.BlueprintFunctions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 UGameDebugBlueprintLibrary 暴露的所有蓝图打印与 3D 绘制节点安全调用
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameDebugBlueprintLibraryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = nullptr;
    if (GEngine != nullptr && !GEngine->GetWorldContexts().IsEmpty()) {
        World = GEngine->GetWorldContexts()[0].World();
    }

    UGameDebugBlueprintLibrary::PrintDebugMessage(TEXT("BP Test Message"), FColor::Cyan, 1.0f);
    UGameDebugBlueprintLibrary::PrintChannelMessage(FName(TEXT("Combat")), TEXT("BP Channel Damage"), FColor::Yellow, 1.0f);
    UGameDebugBlueprintLibrary::PrintWarning(TEXT("BP Warning message"), 2.0f);
    UGameDebugBlueprintLibrary::PrintError(TEXT("BP Error message"), 2.0f);

    UGameDebugBlueprintLibrary::DrawDebugLine(World, FVector::ZeroVector, FVector(100.0, 0.0, 0.0));
    UGameDebugBlueprintLibrary::DrawDebugSphere(World, FVector::ZeroVector, 40.0f);
    UGameDebugBlueprintLibrary::DrawDebugBox(World, FVector::ZeroVector, FVector(30.0, 30.0, 30.0));
    UGameDebugBlueprintLibrary::DrawDebugCapsule(World, FVector::ZeroVector, 60.0f, 25.0f);
    UGameDebugBlueprintLibrary::DrawDebugCylinder(World, FVector::ZeroVector, FVector::UpVector * 80.0, 30.0f);
    UGameDebugBlueprintLibrary::DrawDebugCone(World, FVector::ZeroVector, FVector::ForwardVector, 120.0f, 30.0f, 30.0f);
    UGameDebugBlueprintLibrary::DrawDebugArrow(World, FVector::ZeroVector, FVector::UpVector * 60.0);
    UGameDebugBlueprintLibrary::DrawDebugCoordinateSystem(World, FVector::ZeroVector, FRotator::ZeroRotator);
    UGameDebugBlueprintLibrary::DrawDebugString(World, FVector::ZeroVector, TEXT("BP Floating String"));

    TestTrue(TEXT("蓝图函数库打印与绘制方法调用安全无崩溃"), true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugTimeControllerTest,
    "Game.Debug.Time.GlobalDilationAndGM",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证全局时间控制器膨胀率设置、暂停、恢复与 GM 命令行交互
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameDebugTimeControllerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = FGameDebugTimeController::ResolveWorld(nullptr);
    if (World == nullptr) {
        return true;
    }

    const float InitialScale = FGameDebugTimeController::GetTimeDilation(World);
    TestTrue(TEXT("初始时间缩放为正数"), InitialScale > 0.0f);

    TestTrue(TEXT("设置 0.2 倍速慢动作成功"), FGameDebugTimeController::SetTimeDilation(World, 0.2f));
    TestEqual(TEXT("当前时间膨胀率为 0.2"), FGameDebugTimeController::GetTimeDilation(World), 0.2f);
    TestFalse(TEXT("0.2 倍速不是暂停态"), FGameDebugTimeController::IsPaused(World));

    TestTrue(TEXT("暂停时间成功"), FGameDebugTimeController::Pause(World));
    TestTrue(TEXT("暂停时时间膨胀率小于等于 0.001"), FGameDebugTimeController::GetTimeDilation(World) <= 0.001f);
    TestTrue(TEXT("IsPaused 查询为 true"), FGameDebugTimeController::IsPaused(World));

    TestTrue(TEXT("恢复正常时间成功"), FGameDebugTimeController::Resume(World));
    TestEqual(TEXT("恢复后时间膨胀率为 0.2(记忆恢复)"), FGameDebugTimeController::GetTimeDilation(World), 0.2f);
    TestFalse(TEXT("恢复后不再是暂停态"), FGameDebugTimeController::IsPaused(World));

    TestTrue(TEXT("蓝图设置时间膨胀率成功"), UGameDebugBlueprintLibrary::SetGlobalTimeDilation(World, 0.5f));
    TestEqual(TEXT("蓝图读取时间膨胀率为 0.5"), UGameDebugBlueprintLibrary::GetGlobalTimeDilation(World), 0.5f);
    TestTrue(TEXT("蓝图暂停成功"), UGameDebugBlueprintLibrary::PauseGameTime(World));
    TestTrue(TEXT("蓝图 IsGamePaused 为 true"), UGameDebugBlueprintLibrary::IsGamePaused(World));
    TestTrue(TEXT("蓝图步进 2 帧成功"), UGameDebugBlueprintLibrary::StepGameFrames(World, 2));
    TestTrue(TEXT("蓝图恢复成功"), UGameDebugBlueprintLibrary::ResumeGameTime(World));
    TestTrue(TEXT("蓝图重置时钟成功"), UGameDebugBlueprintLibrary::ResetGameTime(World));
    TestEqual(TEXT("蓝图重置后时钟为 1.0"), UGameDebugBlueprintLibrary::GetGlobalTimeDilation(World), 1.0f);

    FGameCheatRegistry& CheatRegistry = FGameCheatRegistry::Get();
    TestTrue(TEXT("GM.TimeScale 指令已注册"), CheatRegistry.HasCheat(FName(TEXT("GM.TimeScale"))));
    TestTrue(TEXT("执行 GM.TimeScale 0.1 成功"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeScale 0.1")));
    TestEqual(TEXT("GM 指令成功将世界设为 0.1 倍速"), FGameDebugTimeController::GetTimeDilation(World), 0.1f);

    TestTrue(TEXT("执行 GM.TimePause 成功"), CheatRegistry.ExecuteCheat(TEXT("GM.TimePause")));
    TestTrue(TEXT("GM 指令成功暂停时钟"), FGameDebugTimeController::GetTimeDilation(World) <= 0.001f);

    TestTrue(TEXT("执行 GM.TimeResume 成功"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeResume")));
    TestEqual(TEXT("GM 指令成功恢复暂停前 0.1 时钟"), FGameDebugTimeController::GetTimeDilation(World), 0.1f);

    TestTrue(TEXT("执行 GM.TimeReset 成功"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeReset")));
    TestEqual(TEXT("重置后时钟为 1.0"), FGameDebugTimeController::GetTimeDilation(World), 1.0f);

    TestTrue(TEXT("执行 GM.TimeStep 3 成功"), CheatRegistry.ExecuteCheat(TEXT("GM.TimeStep 3")));

    FGameDebugTimeController::SetTimeDilation(World, 1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FScopedDebugStopwatchTest,
    "Game.Debug.Profiling.ScopedStopwatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 RAII 作用域耗时诊断秒表与宏展开
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FScopedDebugStopwatchTest::RunTest([[maybe_unused]] const FString& Parameters) {
    {
        FScopedDebugStopwatch Stopwatch(FName(TEXT("TestScopeImmediate")), 1000.0f);
        TestTrue(TEXT("耗时大于等于 0 毫秒"), Stopwatch.GetElapsedMs() >= 0.0f);
    }

    {
        GAME_DEBUG_SCOPED_TIMER("MacroTimerTest", 500.0f);
        GAME_DEBUG_SCOPED_TIMER_CHANNEL("MacroChannelTimerTest", 500.0f, "TestChannel");
    }

    TestTrue(TEXT("作用域秒表析构与宏执行安全无异常"), true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugTrailRecorderTest,
    "Game.Debug.Visualizer.TrailRecorder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证空间运动轨迹记录器采样、最小距离去重、环形淘汰与基于模拟时间的绘制衰减
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameDebugTrailRecorderTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameDebugTrailRecorder Recorder(4, 2.0f, 10.0f);
    TestEqual(TEXT("初始采样点数量为 0"), Recorder.GetSampleCount(), 0);

    const double T0 = 100.0;
    TestTrue(TEXT("首次添加采样点成功"), Recorder.AddSample(FVector(0.0, 0.0, 0.0), T0));
    TestEqual(TEXT("当前采样点数为 1"), Recorder.GetSampleCount(), 1);

    TestFalse(TEXT("位移未超过 10 单位的微小移动被防抖去重拒绝"), Recorder.AddSample(FVector(2.0, 0.0, 0.0), T0 + 0.1));
    TestEqual(TEXT("去重后采样点数仍为 1"), Recorder.GetSampleCount(), 1);

    TestTrue(TEXT("位移达标采样成功"), Recorder.AddSample(FVector(50.0, 0.0, 0.0), T0 + 0.2));
    TestTrue(TEXT("第 3 点采样成功"), Recorder.AddSample(FVector(100.0, 0.0, 0.0), T0 + 0.3));
    TestTrue(TEXT("第 4 点采样成功达到最大容量"), Recorder.AddSample(FVector(150.0, 0.0, 0.0), T0 + 0.4));
    TestEqual(TEXT("采样点达到上限 4"), Recorder.GetSampleCount(), 4);

    TestTrue(TEXT("第 5 点采样成功触发头部滚动淘汰"), Recorder.AddSample(FVector(200.0, 0.0, 0.0), T0 + 0.5));
    TestEqual(TEXT("滚动淘汰后维持最大容量 4"), Recorder.GetSampleCount(), 4);
    TestEqual(TEXT("最早的点已被淘汰当前首点为 (50, 0, 0)"), Recorder.GetSample(0).Location, FVector(50.0, 0.0, 0.0));

    UWorld* World = nullptr;
    if (GEngine != nullptr && !GEngine->GetWorldContexts().IsEmpty()) {
        World = GEngine->GetWorldContexts()[0].World();
    }

    Recorder.DrawTrail(World, FColor::Red, FColor::Blue, 2.0f, 0.0f, T0 + 0.6);
    TestEqual(TEXT("未超期绘制后采样点依然保留"), Recorder.GetSampleCount(), 4);

    const double TExpire = T0 + 10.0;
    Recorder.DrawTrail(World, FColor::Red, FColor::Blue, 2.0f, 0.0f, TExpire);
    TestEqual(TEXT("超过生命周期的点被自动淘汰清理"), Recorder.GetSampleCount(), 0);

    Recorder.Clear();
    TestEqual(TEXT("显式 Clear 后采样点归零"), Recorder.GetSampleCount(), 0);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
