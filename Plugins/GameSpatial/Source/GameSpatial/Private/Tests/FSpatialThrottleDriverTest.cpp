/**
 * @file FSpatialThrottleDriverTest.cpp
 * @brief FSpatialThrottleDriver 单元测试
 */

#include "Spatial/FSpatialThrottleDriver.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialThrottleDriverDistanceAndSignificanceTest,
    "Game.Spatial.ThrottleDriver.DistanceAndSignificance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证空间视锥驱动器的距离分级、高重要度锁定、视锥背后裁剪与实体剔除
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialThrottleDriverDistanceAndSignificanceTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FThrottledTickScheduler Scheduler;
    FSpatialThrottleDriver Driver(Scheduler);

    TestEqual(TEXT("初始实体数量为 0"), Driver.Num(), 0);

    const uint64 InvalidHandle1 = Driver.RegisterSpatialEntity(nullptr, nullptr, [](float) {});
    TestEqual(TEXT("空位置获取器返回 0"), InvalidHandle1, 0ULL);

    const uint64 InvalidHandle2 = Driver.RegisterSpatialEntity(nullptr, []() { return FVector::ZeroVector; }, nullptr);
    TestEqual(TEXT("空回调函数返回 0"), InvalidHandle2, 0ULL);

    FVector BossLocation(1000.0f, 0.0f, 0.0f);
    int32 BossTickCount = 0;
    const uint64 BossHandle = Driver.RegisterSpatialEntity(
        nullptr,
        [&BossLocation]() { return BossLocation; },
        [&BossTickCount](float) { ++BossTickCount; },
        true
    );
    TestTrue(TEXT("Boss 实体注册有效"), BossHandle > 0ULL);

    FVector MinionLocation(2000.0f, 0.0f, 0.0f);
    int32 MinionTickCount = 0;
    const uint64 MinionHandle = Driver.RegisterSpatialEntity(
        nullptr,
        [&MinionLocation]() { return MinionLocation; },
        [&MinionTickCount](float) { ++MinionTickCount; },
        false
    );
    TestTrue(TEXT("小怪实体注册有效"), MinionHandle > 0ULL);
    TestEqual(TEXT("已注册实体数量为 2"), Driver.Num(), 2);

    FSpatialViewContext View(FVector::ZeroVector, FVector(1.0f, 0.0f, 0.0f), 90.0f, 10000.0f);
    Driver.UpdateSpatialThrottle(View);

    Scheduler.Tick(0.0166f);
    TestEqual(TEXT("Boss 为高重要度，始终满帧触发"), BossTickCount, 1);
    TestEqual(TEXT("Minion 处于 Mid 区间，首帧未达 0.0333s 间隔不触发"), MinionTickCount, 0);

    Scheduler.Tick(0.020f);
    TestEqual(TEXT("Boss 第二帧继续满帧触发"), BossTickCount, 2);
    TestEqual(TEXT("Minion 累积时间超 0.0333s 后触发"), MinionTickCount, 1);

    MinionLocation = FVector(-1000.0f, 0.0f, 0.0f);
    Driver.UpdateSpatialThrottle(View);

    MinionLocation = FVector(20000.0f, 0.0f, 0.0f);
    Driver.UpdateSpatialThrottle(View);

    MinionLocation = FVector(500.0f, 0.0f, 0.0f);
    Driver.UpdateSpatialThrottle(View);

    MinionLocation = FVector(5000.0f, 0.0f, 0.0f);
    Driver.UpdateSpatialThrottle(View);

    TestFalse(TEXT("修改不存在的实体重要度返回 false"), Driver.SetEntitySignificance(9999ULL, true));
    TestTrue(TEXT("动态切换 Minion 为高重要度"), Driver.SetEntitySignificance(MinionHandle, true));

    FSpatialThrottleTierConfig CustomConfig = Driver.GetTierConfig();
    CustomConfig.DistanceNear = 2000.0f;
    Driver.SetTierConfig(CustomConfig);
    TestEqual(TEXT("配置修改生效"), Driver.GetTierConfig().DistanceNear, 2000.0f);

    TestFalse(TEXT("注销 0 句柄返回 false"), Driver.UnregisterSpatialEntity(0ULL));
    TestFalse(TEXT("注销不存在句柄返回 false"), Driver.UnregisterSpatialEntity(9999ULL));
    TestTrue(TEXT("成功注销 Minion"), Driver.UnregisterSpatialEntity(MinionHandle));
    TestEqual(TEXT("注销后剩余实体数量为 1"), Driver.Num(), 1);

    bool bKeepAlive = true;
    const uint64 ExpiringHandle = Driver.RegisterSpatialEntity(
        [&bKeepAlive]() { return bKeepAlive; },
        []() { return FVector::ZeroVector; },
        [](float) {}
    );
    TestTrue(TEXT("生命周期实体注册成功"), ExpiringHandle > 0ULL);
    TestEqual(TEXT("注册后数量为 2"), Driver.Num(), 2);

    bKeepAlive = false;
    Driver.UpdateSpatialThrottle(View);
    TestEqual(TEXT("失效实体被自动剔除"), Driver.Num(), 1);

    Driver.Clear();
    TestEqual(TEXT("清空后实体数量为 0"), Driver.Num(), 0);

    FSpatialThrottleTierConfig HysteresisConfig = Driver.GetTierConfig();
    HysteresisConfig.DistanceNear = 1000.0f;
    HysteresisConfig.DistanceMid = 3000.0f;
    HysteresisConfig.DistanceFar = 6000.0f;
    HysteresisConfig.DistanceHysteresis = 50.0f;
    Driver.SetTierConfig(HysteresisConfig);

    FVector BorderPos(990.0f, 0.0f, 0.0f);
    int32 BorderTicks = 0;
    const uint64 BorderHandle = Driver.RegisterSpatialEntity(
        nullptr,
        [&BorderPos]() { return BorderPos; },
        [&BorderTicks](float) { ++BorderTicks; },
        false
    );
    TestTrue(TEXT("边界测试实体注册成功"), BorderHandle > 0ULL);

    Driver.UpdateSpatialThrottle(View);
    BorderPos = FVector(1030.0f, 0.0f, 0.0f);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.016f);
    TestEqual(TEXT("处于 [1000, 1050] 距离迟滞带内时保持 Near 满帧档位不发生乒乓抖动"), BorderTicks, 1);

    Driver.Clear();

    uint64 SelfUnregisterHandle = 0ULL;
    SelfUnregisterHandle = Driver.RegisterSpatialEntity(
        nullptr,
        [&Driver, &SelfUnregisterHandle]() -> FVector {
            for (int32 SpawnIdx = 0; SpawnIdx < 32; ++SpawnIdx) {
                Driver.RegisterSpatialEntity(
                    nullptr,
                    []() { return FVector(500.0f, 0.0f, 0.0f); },
                    [](float) {}
                );
            }
            Driver.UnregisterSpatialEntity(SelfUnregisterHandle);
            return FVector(200.0f, 0.0f, 0.0f);
        },
        [](float) {}
    );
    TestTrue(TEXT("重入自注销测试实体注册成功"), SelfUnregisterHandle > 0ULL);
    Driver.UpdateSpatialThrottle(View);
    TestEqual(TEXT("PositionGetter 回调内扩容 32 个新实体并自注销自身后零悬垂崩溃且活跃数量精确为 32"), Driver.Num(), 32);

    Driver.Clear();
    return true;
}

#endif
