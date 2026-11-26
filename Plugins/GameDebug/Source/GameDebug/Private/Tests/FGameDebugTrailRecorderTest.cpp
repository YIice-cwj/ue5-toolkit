/**
 * @file FGameDebugTrailRecorderTest.cpp
 * @brief FGameDebugTrailRecorder O(1) 环形缓冲轨迹记录器全边界与可视化真测试
 */

#include "Visualizer/FGameDebugTrailRecorder.h"

#include "Config/GameCompileSwitches.h"
#include "Tests/GameDebugTestUtils.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDebugTrailRecorderTest,
    "Game.Debug.Visualizer.TrailRecorder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证构造参数钳制、t=0.0 仿真时间、NaN 防护、最小距离去重、多轮 O(1) 环形淘汰、单点/部分过期与真实线段及时间颜色衰减绘制
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameDebugTrailRecorderTest::RunTest([[maybe_unused]] const FString& Parameters) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return true;
    }

    FGameDebugTrailRecorder ClampedRecorder(1, -5.0f, -10.0f);
    TestTrue(TEXT("t=0.0 合法仿真时间戳允许写入首点"), ClampedRecorder.AddSample(FVector::ZeroVector, 0.0));
    TestTrue(TEXT("MinDistance 负值钳制为 0 后允许同坐标二次采样"), ClampedRecorder.AddSample(FVector::ZeroVector, 0.05));
    TestTrue(TEXT("MaxSamples 下限钳制为 2 后第 3 点触发环形覆盖"), ClampedRecorder.AddSample(FVector(10.0, 0.0, 0.0), 0.08));
    TestEqual(TEXT("MaxSamples=2 上限维持有效点数为 2"), ClampedRecorder.GetSampleCount(), 2);
    TestEqual(TEXT("首点时间为 0.05"), ClampedRecorder.GetSample(0).Timestamp, 0.05);
    TestEqual(TEXT("末点时间为 0.08"), ClampedRecorder.GetSample(1).Timestamp, 0.08);

    const double QuietNaN = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("NaN 坐标被拦截"), ClampedRecorder.AddSample(FVector(QuietNaN, 0.0, 0.0), 0.09));
    TestFalse(TEXT("NaN 时间戳被拦截"), ClampedRecorder.AddSample(FVector(20.0, 0.0, 0.0), QuietNaN));
    TestEqual(TEXT("拦截非法采样后点数不变"), ClampedRecorder.GetSampleCount(), 2);

    FGameDebugTrailRecorder Recorder(4, 2.0f, 10.0f);
    TestEqual(TEXT("初始采样点数量为 0"), Recorder.GetSampleCount(), 0);

    const double T0 = 100.0;
    TestTrue(TEXT("首次添加采样点成功"), Recorder.AddSample(FVector(0.0, 0.0, 0.0), T0));
    TestFalse(TEXT("微小位移被最小距离阈值拦截"), Recorder.AddSample(FVector(2.0, 0.0, 0.0), T0 + 0.1));
    TestEqual(TEXT("去重后仍为 1 点"), Recorder.GetSampleCount(), 1);

    Recorder.DrawTrail(nullptr, FColor::Green, FColor::Blue, 2.0f, 0.0f, T0 + 5.0);
    TestEqual(TEXT("单个超期采样点在 DrawTrail 中被正确淘汰归零"), Recorder.GetSampleCount(), 0);

    for (int32 Step = 0; Step < 10; ++Step) {
        const FVector Pos(static_cast<double>(Step) * 50.0, 0.0, 0.0);
        TestTrue(TEXT("多轮环形写入成功"), Recorder.AddSample(Pos, T0 + 10.0 + static_cast<double>(Step) * 0.2));
    }
    TestEqual(TEXT("多轮回绕后严格保持容量上限 4"), Recorder.GetSampleCount(), 4);
    TestEqual(TEXT("逻辑索引 0 为第 6 步采样点 (300,0,0)"), Recorder.GetSample(0).Location, FVector(300.0, 0.0, 0.0));
    TestEqual(TEXT("逻辑索引 3 为第 9 步采样点 (450,0,0)"), Recorder.GetSample(3).Location, FVector(450.0, 0.0, 0.0));

    UWorld* World = Game::Debug::Test::GetAutomationTestWorld();
    TestNotNull(TEXT("自动化测试环境存在有效 UWorld"), World);
    if (World == nullptr) {
        return false;
    }

    Game::Debug::Test::FlushWorldDebugLines(World);
    const double DrawTime = T0 + 11.8;
    Recorder.DrawTrail(World, FColor::Red, FColor::Blue, 2.0f, 0.0f, DrawTime);

    const ULineBatchComponent* FrameBatcher = Game::Debug::Test::GetWorldDebugLineBatcher(World, false);
    TestNotNull(TEXT("World LineBatcher 有效"), FrameBatcher);
    if (FrameBatcher != nullptr) {
        TestEqual(TEXT("4 个有效点精确绘制 3 条连线段"), FrameBatcher->BatchedLines.Num(), 3);
        if (FrameBatcher->BatchedLines.Num() == 3) {
            const FColor OldestSegColor = FrameBatcher->BatchedLines[0].Color.ToFColor(true);
            const FColor NewestSegColor = FrameBatcher->BatchedLines[2].Color.ToFColor(true);
            TestTrue(TEXT("时间衰减渐变：最新段红色分量严格高于最旧段"), NewestSegColor.R > OldestSegColor.R);
            TestTrue(TEXT("时间衰减渐变：最旧段蓝色分量严格高于最新段"), OldestSegColor.B > NewestSegColor.B);
        }
    }

    Recorder.DrawTrail(World, FColor::Red, FColor::Blue, 2.0f, 0.0f, T0 + 13.5);
    TestEqual(TEXT("部分过期后仅保留未超期的最新 2 点"), Recorder.GetSampleCount(), 2);

    Recorder.DrawTrail(World, FColor::Red, FColor::Blue, 2.0f, 0.0f, T0 + 20.0);
    TestEqual(TEXT("全部超期后自动清空至 0 点"), Recorder.GetSampleCount(), 0);

    TestTrue(TEXT("乱序时间戳保护：写入首点 t=50.0"), Recorder.AddSample(FVector(0.0, 0.0, 0.0), 50.0));
    TestTrue(TEXT("乱序时间戳保护：写入回退时间戳 t=40.0 被单调钳制为 50.0"), Recorder.AddSample(FVector(50.0, 0.0, 0.0), 40.0));
    TestEqual(TEXT("回退时间戳被钳制保持单调非递减"), Recorder.GetSample(1).Timestamp, 50.0);

    Game::Debug::Test::FlushWorldDebugLines(World);
    Recorder.DrawTrail(World, FColor::Red, FColor::Blue, -2.0f, 0.0f, 50.5);
    Recorder.DrawTrail(World, FColor::Red, FColor::Blue, 2.0f, -1.0f, 50.5);
    TestEqual(TEXT("非法负 Thickness/Duration 在循环前快速拦截，写入线段数为 0"), Game::Debug::Test::GetTotalBatchedLineCount(World), 0);

    Recorder.Clear();
    TestEqual(TEXT("显式 Clear 后采样点归零"), Recorder.GetSampleCount(), 0);

    Game::Debug::Test::FlushWorldDebugLines(World);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
