/**
 * @file FSpatialThrottleDriverTest.cpp
 * @brief FSpatialThrottleDriver 全档位判定、双向迟滞带、配置钳制、回调重入与弱引用子系统安全单元测试
 */

#include "Throttle/FSpatialThrottleDriver.h"

#if WITH_AUTOMATION_TESTS

#include <limits>
#include "Async/UThrottledTickerSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace {

UWorld* GetThrottleTestWorld() {
    if (GEngine == nullptr) {
        return nullptr;
    }
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World() != nullptr) {
            return Context.World();
        }
    }
    return nullptr;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialThrottleDriverDistanceTiersTest,
    "Game.Spatial.ThrottleDriver.DistanceTiersAndSignificance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证全部四级距离档位（含 DistanceFar 上限）、高重要度保真、背后/负视距/零朝向裁剪与 O(1) 注销
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialThrottleDriverDistanceTiersTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FThrottledTickScheduler Scheduler;
    FSpatialThrottleDriver Driver(Scheduler);

    TestEqual(TEXT("初始实体数量为 0"), Driver.Num(), 0);
    TestFalse(TEXT("0 句柄未注册"), Driver.IsEntityRegistered(0ULL));

    TestEqual(TEXT("空位置获取器拒绝注册"), Driver.RegisterSpatialEntity(nullptr, nullptr, [](float) {}), 0ULL);
    TestEqual(TEXT("空回调拒绝注册"), Driver.RegisterSpatialEntity(nullptr, []() { return FVector::ZeroVector; }, nullptr), 0ULL);
    TestEqual(
        TEXT("初始失效校验器拒绝注册"),
        Driver.RegisterSpatialEntity([]() { return false; }, []() { return FVector::ZeroVector; }, [](float) {}),
        0ULL
    );

    FVector BossLocation(-20000.0, 0.0, 0.0);
    int32 BossTicks = 0;
    const uint64 BossHandle = Driver.RegisterSpatialEntity(
        nullptr,
        [&BossLocation]() { return BossLocation; },
        [&BossTicks](float) { ++BossTicks; },
        true
    );
    TestTrue(TEXT("Boss 句柄有效"), BossHandle > 0ULL);
    TestTrue(TEXT("Boss 已注册"), Driver.IsEntityRegistered(BossHandle));

    FVector MinionLocation(2000.0, 0.0, 0.0);
    int32 MinionTicks = 0;
    const uint64 MinionHandle = Driver.RegisterSpatialEntity(
        nullptr,
        [&MinionLocation]() { return MinionLocation; },
        [&MinionTicks](float) { ++MinionTicks; },
        false
    );
    TestTrue(TEXT("Minion 句柄有效"), MinionHandle > 0ULL);
    TestEqual(TEXT("总实体数为 2"), Driver.Num(), 2);

    const FSpatialViewContext View(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 90.0f, 10000.0f);
    Driver.UpdateSpatialThrottle(View);

    Scheduler.Tick(0.016f);
    TestEqual(TEXT("Boss 高重要度在背后极远处依然满帧触发"), BossTicks, 1);
    TestEqual(TEXT("Minion 在 Mid(2000) 处 0.016s 不触发"), MinionTicks, 0);

    Scheduler.Tick(0.020f);
    TestEqual(TEXT("Minion 在 Mid 处累积超过 0.0333s 后触发"), MinionTicks, 1);

    MinionTicks = 0;
    MinionLocation = FVector(5000.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.040f);
    TestEqual(TEXT("Minion 切至 Far(5000) 后 0.040s 未达 0.0666s 不触发"), MinionTicks, 0);
    Scheduler.Tick(0.035f);
    TestEqual(TEXT("Minion 在 Far 处超过 0.0666s 后触发"), MinionTicks, 1);

    MinionTicks = 0;
    MinionLocation = FVector(7500.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.20f);
    TestEqual(TEXT("超出 DistanceFar(6000) 但小于 ViewMaxDistance(10000) 时必须降级为 OutOfRange(0.5s)"), MinionTicks, 0);
    Scheduler.Tick(0.35f);
    TestEqual(TEXT("在 OutOfRange 累积超 0.5s 后触发"), MinionTicks, 1);

    MinionTicks = 0;
    MinionLocation = FVector(-1000.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.20f);
    TestEqual(TEXT("处于摄像机背后(-1000) 保持 OutOfRange 不触发"), MinionTicks, 0);

    MinionTicks = 0;
    MinionLocation = FVector(500.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.016f);
    TestEqual(TEXT("回到 Near(500) 后恢复每帧满速触发"), MinionTicks, 1);

    MinionTicks = 0;
    MinionLocation = FVector(0.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(FSpatialViewContext(FVector::ZeroVector, FVector::ZeroVector, 0.0f, -100.0f));
    Scheduler.Tick(0.016f);
    TestEqual(TEXT("与观察点重合实体维持 Near 满帧"), MinionTicks, 1);

    MinionTicks = 0;
    MinionLocation = FVector(50.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(FSpatialViewContext(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 90.0f, -100.0f));
    Scheduler.Tick(0.20f);
    TestEqual(TEXT("负 MaxDistance 不会被平方变正，正确降级为 OutOfRange"), MinionTicks, 0);

    MinionTicks = 0;
    Driver.UpdateSpatialThrottle(FSpatialViewContext(FVector::ZeroVector, FVector::ZeroVector, 240.0f, 10000.0f));
    Scheduler.Tick(0.20f);
    TestEqual(TEXT("零 ViewForward 即使大 FOV 也正确降级为 OutOfRange"), MinionTicks, 0);

    MinionTicks = 0;
    MinionLocation = FVector(100.0, 100.0, 0.0);
    Driver.UpdateSpatialThrottle(FSpatialViewContext(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 90.0f, 10000.0f));
    Scheduler.Tick(0.016f);
    TestEqual(TEXT("FOV=90度时精确 45度边界实体判定进入 Near"), MinionTicks, 1);

    MinionLocation = FVector(-1000.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(FSpatialViewContext(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 90.0f, 10000.0f));
    MinionTicks = 0;
    MinionLocation = FVector(0.0, 100.0, 0.0);
    Driver.UpdateSpatialThrottle(FSpatialViewContext(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 180.0f, 10000.0f));
    Scheduler.Tick(0.016f);
    TestEqual(TEXT("FOV=180度时精确 90度正侧方边界实体判定进入 Near"), MinionTicks, 1);

    MinionLocation = FVector(-1000.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(FSpatialViewContext(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 90.0f, 10000.0f));
    MinionTicks = 0;
    MinionLocation = FVector(-100.0, -200.0, -300.0);
    Driver.UpdateSpatialThrottle(FSpatialViewContext(FVector::ZeroVector, FVector(1.0, 2.0, 3.0), 360.0f, 10000.0f));
    Scheduler.Tick(0.016f);
    TestEqual(TEXT("FOV=360度全向视锥下斜向正背后实体判定进入 Near"), MinionTicks, 1);

    const double NaNVal = std::numeric_limits<double>::quiet_NaN();
    MinionLocation = FVector(500.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    MinionTicks = 0;
    Driver.UpdateSpatialThrottle(FSpatialViewContext(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 0.0f, 10000.0f));
    Scheduler.Tick(0.10f);
    TestEqual(TEXT("FOV=0度时已处于 Near 的正前方实体也不被迟滞带保留，正确降级为 OutOfRange"), MinionTicks, 0);

    Driver.UpdateSpatialThrottle(View);
    MinionTicks = 0;
    Driver.UpdateSpatialThrottle(FSpatialViewContext(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), -90.0f, 10000.0f));
    Scheduler.Tick(0.10f);
    TestEqual(TEXT("负 FOV 时已处于 Near 的正前方实体正确降级为 OutOfRange"), MinionTicks, 0);

    Driver.UpdateSpatialThrottle(View);
    MinionTicks = 0;
    Driver.UpdateSpatialThrottle(FSpatialViewContext(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), static_cast<float>(NaNVal), 10000.0f));
    Scheduler.Tick(0.10f);
    TestEqual(TEXT("NaN FOV 时已处于 Near 的正前方实体正确降级为 OutOfRange"), MinionTicks, 0);

    Driver.UpdateSpatialThrottle(View);
    MinionTicks = 0;
    MinionLocation = FVector(NaNVal, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.10f);
    TestEqual(TEXT("NaN 实体坐标安全降级为 OutOfRange"), MinionTicks, 0);
    MinionLocation = FVector(50.0, 0.0, 0.0);

    TestFalse(TEXT("修改 0 句柄重要度返回 false"), Driver.SetEntitySignificance(0ULL, true));
    TestFalse(TEXT("修改不存在句柄重要度返回 false"), Driver.SetEntitySignificance(9999ULL, true));
    TestTrue(TEXT("动态切换 Minion 为高重要度"), Driver.SetEntitySignificance(MinionHandle, true));
    Driver.UpdateSpatialThrottle(FSpatialViewContext(FVector::ZeroVector, FVector::ZeroVector, 90.0f, -100.0f));
    MinionTicks = 0;
    Scheduler.Tick(0.016f);
    TestEqual(TEXT("高重要度生效后立即恢复满帧"), MinionTicks, 1);

    const uint64 ThirdHandle = Driver.RegisterSpatialEntity(nullptr, []() { return FVector(200.0, 0.0, 0.0); }, [](float) {});
    TestEqual(TEXT("注册第 3 个实体后总数为 3"), Driver.Num(), 3);
    TestFalse(TEXT("注销 0 句柄返回 false"), Driver.UnregisterSpatialEntity(0ULL));
    TestFalse(TEXT("注销不存在句柄返回 false"), Driver.UnregisterSpatialEntity(9999ULL));
    TestTrue(TEXT("O(1) 注销中间实体 Minion 成功"), Driver.UnregisterSpatialEntity(MinionHandle));
    TestFalse(TEXT("注销后 Minion 未注册"), Driver.IsEntityRegistered(MinionHandle));
    TestTrue(TEXT("换位后的尾部实体 Third 仍可正常注销"), Driver.UnregisterSpatialEntity(ThirdHandle));
    TestEqual(TEXT("仅剩 Boss 1 个实体"), Driver.Num(), 1);

    Driver.Clear();
    TestEqual(TEXT("Clear 后总数为 0"), Driver.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialThrottleDriverHysteresisAndConfigTest,
    "Game.Spatial.ThrottleDriver.HysteresisAndConfigClamping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Near/Mid/Far/OutOfRange 距离迟滞带、FOV 角度双向迟滞带及非法配置参数钳制
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialThrottleDriverHysteresisAndConfigTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FThrottledTickScheduler Scheduler;
    FSpatialThrottleDriver Driver(Scheduler);

    const float NaNVal = std::numeric_limits<float>::quiet_NaN();
    FSpatialThrottleTierConfig BadConfig;
    BadConfig.DistanceNear = -100.0f;
    BadConfig.DistanceMid = -50.0f;
    BadConfig.DistanceFar = -10.0f;
    BadConfig.DistanceHysteresis = NaNVal;
    BadConfig.NearInterval = -1.0f;
    BadConfig.MidInterval = NaNVal;
    BadConfig.FarInterval = -2.0f;
    BadConfig.OutOfRangeOrBehindInterval = -3.0f;
    BadConfig.HysteresisDegrees = 180.0f;
    Driver.SetTierConfig(BadConfig);

    const FSpatialThrottleTierConfig& Clamped = Driver.GetTierConfig();
    TestEqual(TEXT("负 DistanceNear 钳制为 0"), Clamped.DistanceNear, 0.0f);
    TestEqual(TEXT("DistanceMid >= DistanceNear"), Clamped.DistanceMid, 0.0f);
    TestEqual(TEXT("DistanceFar >= DistanceMid"), Clamped.DistanceFar, 0.0f);
    TestEqual(TEXT("NaN 迟滞带钳制为 0"), Clamped.DistanceHysteresis, 0.0f);
    TestEqual(TEXT("HysteresisDegrees 钳制至上限 90"), Clamped.HysteresisDegrees, 90.0f);

    FSpatialThrottleTierConfig Config;
    Config.DistanceNear = 1000.0f;
    Config.DistanceMid = 3000.0f;
    Config.DistanceFar = 6000.0f;
    Config.DistanceHysteresis = 50.0f;
    Config.NearInterval = 0.0f;
    Config.MidInterval = 0.1f;
    Config.FarInterval = 0.25f;
    Config.OutOfRangeOrBehindInterval = 1.0f;
    Config.HysteresisDegrees = 5.0f;
    Driver.SetTierConfig(Config);

    FVector EntityPos(990.0, 0.0, 0.0);
    int32 Ticks = 0;
    Driver.RegisterSpatialEntity(nullptr, [&EntityPos]() { return EntityPos; }, [&Ticks](float) { ++Ticks; }, false);

    const FSpatialViewContext View(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 90.0f, 10000.0f);
    Driver.UpdateSpatialThrottle(View);

    Ticks = 0;
    EntityPos = FVector(1030.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.016f);
    TestEqual(TEXT("Near->Mid 迟滞带 [1000,1050] 内保持 Near"), Ticks, 1);

    Ticks = 0;
    EntityPos = FVector(1060.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.05f);
    TestEqual(TEXT("跨过 1050 降级为 Mid(0.1s)"), Ticks, 0);

    Ticks = 0;
    EntityPos = FVector(970.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.04f);
    TestEqual(TEXT("Mid->Near 迟滞带 [950,1000] 内保持 Mid 防止抖动"), Ticks, 0);

    Ticks = 0;
    EntityPos = FVector(3030.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.12f);
    TestEqual(TEXT("Mid->Far 迟滞带 [3000,3050] 内保持 Mid(0.1s)"), Ticks, 1);

    Ticks = 0;
    EntityPos = FVector(3060.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.12f);
    TestEqual(TEXT("跨过 3050 降级为 Far(0.25s)"), Ticks, 0);

    Ticks = 0;
    EntityPos = FVector(6030.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.30f);
    TestEqual(TEXT("Far->OutOfRange 迟滞带 [6000,6050] 内保持 Far(0.25s)"), Ticks, 1);

    Ticks = 0;
    EntityPos = FVector(6060.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.30f);
    TestEqual(TEXT("跨过 6050 降级为 OutOfRange(1.0s)"), Ticks, 0);

    Ticks = 0;
    EntityPos = FVector(6030.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.30f);
    TestEqual(TEXT("OutOfRange->Far 回退至 6030 未跨入 6000 保持 OutOfRange"), Ticks, 0);

    Ticks = 0;
    EntityPos = FVector(5990.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.30f);
    TestEqual(TEXT("回退至 5990 进入 Far(0.25s)"), Ticks, 1);

    Ticks = 0;
    EntityPos = FVector(2970.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.12f);
    TestEqual(TEXT("Far->Mid 迟滞带 [2950,3000] 内保持 Far(0.25s) 防止抖动"), Ticks, 0);

    Ticks = 0;
    EntityPos = FVector(2940.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);
    Scheduler.Tick(0.12f);
    TestEqual(TEXT("回退至 2940 跨过迟滞带恢复 Mid(0.1s)"), Ticks, 1);

    EntityPos = FVector(7000.0, 0.0, 0.0);
    Driver.UpdateSpatialThrottle(View);

    const double Angle40Rad = FMath::DegreesToRadians(40.0);
    const double Angle47Rad = FMath::DegreesToRadians(47.0);
    const double Angle52Rad = FMath::DegreesToRadians(52.0);

    EntityPos = FVector(500.0 * FMath::Cos(Angle40Rad), 500.0 * FMath::Sin(Angle40Rad), 0.0);
    Driver.UpdateSpatialThrottle(View);
    Ticks = 0;
    Scheduler.Tick(0.016f);
    TestEqual(TEXT("40度处于 FOV=90(半角45度) 内进入 Near"), Ticks, 1);

    EntityPos = FVector(500.0 * FMath::Cos(Angle47Rad), 500.0 * FMath::Sin(Angle47Rad), 0.0);
    Driver.UpdateSpatialThrottle(View);
    Ticks = 0;
    Scheduler.Tick(0.016f);
    TestEqual(TEXT("外移至 47度处于 [45,50] 角度迟滞带内保持 Near"), Ticks, 1);

    EntityPos = FVector(500.0 * FMath::Cos(Angle52Rad), 500.0 * FMath::Sin(Angle52Rad), 0.0);
    Driver.UpdateSpatialThrottle(View);
    Ticks = 0;
    Scheduler.Tick(0.20f);
    TestEqual(TEXT("外移至 52度超出 50度退出阈值降级为 OutOfRange"), Ticks, 0);

    EntityPos = FVector(500.0 * FMath::Cos(Angle47Rad), 500.0 * FMath::Sin(Angle47Rad), 0.0);
    Driver.UpdateSpatialThrottle(View);
    Ticks = 0;
    Scheduler.Tick(0.20f);
    TestEqual(TEXT("从 OutOfRange 回摆至 47度仍未跨过 45度进入阈值保持 OutOfRange"), Ticks, 0);

    Driver.Clear();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialThrottleDriverReentrancyAndSubsystemTest,
    "Game.Spatial.ThrottleDriver.ReentrancyAndSubsystemSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证校验器失效自动剔除、回调重入增删改清防护与 UThrottledTickerSubsystem 弱引用销毁安全
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialThrottleDriverReentrancyAndSubsystemTest::RunTest([[maybe_unused]] const FString& Parameters) {
    AddExpectedMessage(TEXT("检测到 UpdateSpatialThrottle 递归重入调用"), EAutomationExpectedMessageFlags::Contains, 1);
    AddExpectedMessage(TEXT("观察者上下文包含非有限浮点坐标/朝向"), EAutomationExpectedMessageFlags::Contains, 1);
    AddExpectedMessage(TEXT("绑定的 UThrottledTickerSubsystem 已失效销毁"), EAutomationExpectedMessageFlags::Contains, 1);

    FThrottledTickScheduler Scheduler;
    FSpatialThrottleDriver Driver(Scheduler);
    const FSpatialViewContext View(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 90.0f, 10000.0f);

    const double NaNVal = std::numeric_limits<double>::quiet_NaN();
    Driver.UpdateSpatialThrottle(FSpatialViewContext(FVector(NaNVal, 0.0, 0.0), FVector(1.0, 0.0, 0.0), 90.0f, 10000.0f));

    bool bAlive = true;
    const uint64 ExpiringHandle = Driver.RegisterSpatialEntity(
        [&bAlive]() { return bAlive; },
        []() { return FVector(200.0, 0.0, 0.0); },
        [](float) {}
    );
    TestTrue(TEXT("生命周期实体注册成功"), Driver.IsEntityRegistered(ExpiringHandle));
    bAlive = false;
    Driver.UpdateSpatialThrottle(View);
    TestEqual(TEXT("失效实体被自动剔除"), Driver.Num(), 0);
    TestFalse(TEXT("剔除后句柄无效"), Driver.IsEntityRegistered(ExpiringHandle));

    uint64 ValidatorSelfUnregHandle = 0ULL;
    bool bValidatorFirstRun = true;
    bool bPositionGetterInvoked = false;
    ValidatorSelfUnregHandle = Driver.RegisterSpatialEntity(
        [&]() {
            if (bValidatorFirstRun) {
                bValidatorFirstRun = false;
                return true;
            }
            Driver.UnregisterSpatialEntity(ValidatorSelfUnregHandle);
            return false;
        },
        [&bPositionGetterInvoked]() {
            bPositionGetterInvoked = true;
            return FVector(200.0, 0.0, 0.0);
        },
        [](float) {}
    );
    Driver.UpdateSpatialThrottle(View);
    TestEqual(TEXT("Validator 内自注销后总数精确归零无重复扣减"), Driver.Num(), 0);
    TestFalse(TEXT("Validator 内自注销后不再调用 PositionGetter"), bPositionGetterInvoked);

    uint64 SelfHandle = 0ULL;
    uint64 PendingKeepHandle = 0ULL;
    uint64 PendingCancelHandle = 0ULL;
    bool bPendingRegisteredInside = false;
    bool bPendingSignificanceInside = false;

    SelfHandle = Driver.RegisterSpatialEntity(
        nullptr,
        [&]() -> FVector {
            Driver.UpdateSpatialThrottle(View);
            PendingKeepHandle = Driver.RegisterSpatialEntity(
                nullptr,
                []() { return FVector(500.0, 0.0, 0.0); },
                [](float) {}
            );
            PendingCancelHandle = Driver.RegisterSpatialEntity(
                nullptr,
                []() { return FVector(500.0, 0.0, 0.0); },
                [](float) {}
            );
            bPendingRegisteredInside = Driver.IsEntityRegistered(PendingKeepHandle);
            bPendingSignificanceInside = Driver.SetEntitySignificance(PendingKeepHandle, true);
            Driver.UnregisterSpatialEntity(PendingCancelHandle);
            for (int32 SpawnIdx = 0; SpawnIdx < 31; ++SpawnIdx) {
                Driver.RegisterSpatialEntity(nullptr, []() { return FVector(500.0, 0.0, 0.0); }, [](float) {});
            }
            Driver.UnregisterSpatialEntity(SelfHandle);
            return FVector(200.0, 0.0, 0.0);
        },
        [](float) {}
    );

    Driver.UpdateSpatialThrottle(View);
    TestTrue(TEXT("回调内重入新注册句柄立即可查"), bPendingRegisteredInside);
    TestTrue(TEXT("回调内对 PendingAdditions 句柄修改重要度成功"), bPendingSignificanceInside);
    TestTrue(TEXT("保留句柄刷入活跃数组后仍可查"), Driver.IsEntityRegistered(PendingKeepHandle));
    TestFalse(TEXT("回调内即时取消的 Pending 句柄不存在"), Driver.IsEntityRegistered(PendingCancelHandle));
    TestEqual(TEXT("扩容 32 个并自注销后精确剩余 32 个实体"), Driver.Num(), 32);

    Driver.Clear();
    uint64 RecreatedHandle = 0ULL;
    Driver.RegisterSpatialEntity(
        nullptr,
        [&]() -> FVector {
            Driver.Clear();
            RecreatedHandle = Driver.RegisterSpatialEntity(nullptr, []() { return FVector(100.0, 0.0, 0.0); }, [](float) {});
            return FVector(100.0, 0.0, 0.0);
        },
        [](float) {}
    );
    Driver.UpdateSpatialThrottle(View);
    TestEqual(TEXT("回调内重入 Clear 并重新注册后精确保留 1 个新实体"), Driver.Num(), 1);
    TestTrue(TEXT("重注册实体句柄有效"), Driver.IsEntityRegistered(RecreatedHandle));
    Driver.Clear();

    UWorld* World = GetThrottleTestWorld();
    TestNotNull(TEXT("测试世界上下文有效"), World);
    if (World != nullptr) {
        if (UThrottledTickerSubsystem* TickerSubsystem = World->GetSubsystem<UThrottledTickerSubsystem>()) {
            FSpatialThrottleDriver SubsystemDriver(*TickerSubsystem);
            const uint64 SubHandle = SubsystemDriver.RegisterSpatialEntity(
                nullptr,
                []() { return FVector(300.0, 0.0, 0.0); },
                [](float) {}
            );
            TestTrue(TEXT("绑定世界节流子系统注册成功"), SubHandle > 0ULL);
            SubsystemDriver.UpdateSpatialThrottle(View);
            TestTrue(TEXT("注销子系统驱动实体成功"), SubsystemDriver.UnregisterSpatialEntity(SubHandle));
        }
    }

    UThrottledTickerSubsystem* TransientSubsystem = NewObject<UThrottledTickerSubsystem>();
    FSpatialThrottleDriver ExpiredDriver(*TransientSubsystem);
    TestTrue(
        TEXT("临时子系统注册实体成功"),
        ExpiredDriver.RegisterSpatialEntity(nullptr, []() { return FVector::ZeroVector; }, [](float) {}) > 0ULL
    );
    TransientSubsystem->MarkAsGarbage();
    ExpiredDriver.UpdateSpatialThrottle(View);
    TestEqual(TEXT("绑定子系统销毁失效后自动清空实体且零悬垂崩溃"), ExpiredDriver.Num(), 0);
    TestEqual(
        TEXT("绑定子系统失效后拒绝新注册"),
        ExpiredDriver.RegisterSpatialEntity(nullptr, []() { return FVector::ZeroVector; }, [](float) {}),
        0ULL
    );

    return true;
}

#endif
