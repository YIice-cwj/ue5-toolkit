/**
 * @file FGameMathTest.cpp
 * @brief FGameMath 帧率无关临界阻尼平滑与空间几何工具单元测试
 */

#include "Math/FGameMath.h"

#if WITH_AUTOMATION_TESTS

#include <limits>

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMathCriticalSpringDampTest,
    "Game.Core.Math.CriticalSpringDamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证标量与三维临界阻尼弹簧的帧率无关性、无超调单调收敛、负脉冲响应与零步长守卫
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameMathCriticalSpringDampTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameMath::FSpringFloatState InitialFloat{10.0f, -5.0f};
    const FGameMath::FSpringFloatState ZeroDtFloat = FGameMath::CriticalSpringDampFloat(InitialFloat, 100.0f, 0.25f, 0.0f);
    TestEqual(TEXT("零帧长保持原标量值不变"), ZeroDtFloat.Value, 10.0f);
    TestEqual(TEXT("零帧长保持原速度不变"), ZeroDtFloat.Velocity, -5.0f);

    FGameMath::FSpringFloatState HighFpsState{0.0f, 0.0f};
    for (int32 Step = 0; Step < 120; ++Step) {
        HighFpsState = FGameMath::CriticalSpringDampFloat(HighFpsState, 100.0f, 0.2f, 1.0f / 120.0f);
        TestTrue(TEXT("从静止收敛全程绝不超调过 100"), HighFpsState.Value <= 100.0f);
    }

    FGameMath::FSpringFloatState LowFpsState{0.0f, 0.0f};
    for (int32 Step = 0; Step < 30; ++Step) {
        LowFpsState = FGameMath::CriticalSpringDampFloat(LowFpsState, 100.0f, 0.2f, 1.0f / 30.0f);
    }
    TestTrue(TEXT("120Hz 与 30Hz 经过相同 1 秒物理时长后位置高度一致（帧率无关）"), FMath::IsNearlyEqual(HighFpsState.Value, LowFpsState.Value, 0.2f));
    TestTrue(TEXT("1 秒后已高度收敛至目标值 100 附近"), FMath::IsNearlyEqual(HighFpsState.Value, 100.0f, 0.5f));

    const FGameMath::FSpringFloatState ImpulseStep =
        FGameMath::CriticalSpringDampFloat(FGameMath::FSpringFloatState{50.0f, -100.0f}, 50.0f, 0.2f, 0.016f);
    TestTrue(TEXT("平衡点携带负初速度脉冲时正常产生负位移响应"), ImpulseStep.Value < 50.0f);

    const FGameMath::FSpringVectorState InitialVec{FVector(1.0, 2.0, 3.0), FVector::ZeroVector};
    const FGameMath::FSpringVectorState ZeroDtVec =
        FGameMath::CriticalSpringDampVector(InitialVec, FVector(100.0, 200.0, 300.0), 0.2f, -0.01f);
    TestTrue(TEXT("负帧长保持原三维向量不变"), ZeroDtVec.Value.Equals(InitialVec.Value, 1e-6f));

    FGameMath::FSpringVectorState VecState{FVector::ZeroVector, FVector::ZeroVector};
    const FVector TargetVec(300.0, -150.0, 80.0);
    for (int32 Step = 0; Step < 60; ++Step) {
        VecState = FGameMath::CriticalSpringDampVector(VecState, TargetVec, 0.15f, 1.0f / 60.0f);
    }
    TestTrue(TEXT("三维阻尼弹簧平滑收敛至目标点"), VecState.Value.Equals(TargetVec, 0.5));

    const float NanVal = std::numeric_limits<float>::quiet_NaN();
    const FGameMath::FSpringFloatState NanState{NanVal, 10.0f};
    const FGameMath::FSpringFloatState RecoveredFloat = FGameMath::CriticalSpringDampFloat(NanState, 50.0f, 0.2f, 0.016f);
    TestEqual(TEXT("标量弹簧输入 NaN 安全重置为目标值"), RecoveredFloat.Value, 50.0f);
    TestEqual(TEXT("标量弹簧输入 NaN 安全重置速度为 0"), RecoveredFloat.Velocity, 0.0f);

    const FGameMath::FSpringVectorState NanVecState{FVector(NAN, 0.0, 0.0), FVector::ZeroVector};
    const FGameMath::FSpringVectorState RecoveredVec = FGameMath::CriticalSpringDampVector(NanVecState, FVector(10.0, 20.0, 30.0), 0.2f, 0.016f);
    TestTrue(TEXT("向量弹簧输入 NaN 安全重置为目标向量"), RecoveredVec.Value.Equals(FVector(10.0, 20.0, 30.0), 1e-4));
    TestTrue(TEXT("向量弹簧输入 NaN 安全重置速度为零"), RecoveredVec.Velocity.IsZero());

    const FGameMath::FSpringFloatState StateA{1.0f, 2.0f};
    const FGameMath::FSpringFloatState StateB{1.0f, 2.0f};
    TestTrue(TEXT("FSpringFloatState 等号运算符一致"), StateA == StateB);

    const FGameMath::FSpringVectorState VecA{FVector(1, 2, 3), FVector(4, 5, 6)};
    const FGameMath::FSpringVectorState VecB{FVector(1, 2, 3), FVector(4, 5, 6)};
    TestTrue(TEXT("FSpringVectorState 等号运算符一致"), VecA == VecB);

    FGameMath::FSpringFloatState AngleState{170.0f, 0.0f};
    for (int32 Step = 0; Step < 60; ++Step) {
        AngleState = FGameMath::CriticalSpringDampAngleDegrees(AngleState, -170.0f, 0.1f, 1.0f / 60.0f);
    }
    TestTrue(TEXT("角度弹簧沿最短弧度收敛至 -170 附近"), FMath::IsNearlyEqual(AngleState.Value, -170.0f, 1.0f));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMathSpatialGeometryTest,
    "Game.Core.Math.SpatialGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证水平有符号偏航角计算与三维视锥扇形距离/角度边界判定
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameMathSpatialGeometryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FVector ForwardX(1.0, 0.0, 5.0);
    TestTrue(TEXT("纯竖直零水平投影返回 0 度"), FMath::IsNearlyEqual(FGameMath::SignedYawDeltaDegrees(FVector(0, 0, 1), ForwardX), 0.0f));
    TestTrue(TEXT("同方向水平偏航差为 0 度"), FMath::IsNearlyEqual(FGameMath::SignedYawDeltaDegrees(ForwardX, FVector(2, 0, -3)), 0.0f, 1e-3f));
    TestTrue(TEXT("正 Y 侧偏航差为 +90 度"), FMath::IsNearlyEqual(FGameMath::SignedYawDeltaDegrees(ForwardX, FVector(0, 1, 0)), 90.0f, 1e-3f));
    TestTrue(TEXT("负 Y 侧偏航差为 -90 度"), FMath::IsNearlyEqual(FGameMath::SignedYawDeltaDegrees(ForwardX, FVector(0, -1, 0)), -90.0f, 1e-3f));
    TestTrue(TEXT("正后方偏航差绝对值为 180 度"), FMath::IsNearlyEqual(FMath::Abs(FGameMath::SignedYawDeltaDegrees(ForwardX, FVector(-1, 0, 0))), 180.0f, 1e-3f));

    const FVector AxisX(1.0, 0.0, 0.0);
    TestFalse(TEXT("非正视距恒不在视锥内"), FGameMath::IsInVisionCone(FVector(10, 0, 0), AxisX, 0.0f, 45.0f));
    TestFalse(TEXT("负半张角恒不在视锥内"), FGameMath::IsInVisionCone(FVector(10, 0, 0), AxisX, 100.0f, -1.0f));
    TestFalse(TEXT("零朝向轴向量恒不在视锥内"), FGameMath::IsInVisionCone(FVector(10, 0, 0), FVector::ZeroVector, 100.0f, 45.0f));
    TestFalse(TEXT("超出最大视距被拦截"), FGameMath::IsInVisionCone(FVector(101.0, 0.0, 0.0), AxisX, 100.0f, 45.0f));
    TestTrue(TEXT("与视点重合点命中视锥"), FGameMath::IsInVisionCone(FVector::ZeroVector, AxisX, 100.0f, 10.0f));
    TestTrue(TEXT("180 度全向球视域内命中后方目标"), FGameMath::IsInVisionCone(FVector(-50.0, 0.0, 0.0), AxisX, 100.0f, 180.0f));
    TestTrue(TEXT("44 度目标命中 45 度半张角视锥"), FGameMath::IsInVisionCone(FVector(50.0, 48.0, 0.0), AxisX, 100.0f, 45.0f));
    TestFalse(TEXT("60 度目标被 45 度半张角视锥排除"), FGameMath::IsInVisionCone(FVector(40.0, 70.0, 0.0), AxisX, 100.0f, 45.0f));

    const float NanAngle = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("NaN 视距恒不在视锥内"), FGameMath::IsInVisionCone(FVector(10, 0, 0), AxisX, NanAngle, 45.0f));
    TestFalse(TEXT("NaN 半张角恒不在视锥内"), FGameMath::IsInVisionCone(FVector(10, 0, 0), AxisX, 100.0f, NanAngle));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameMathExpDecayTest,
    "Game.Core.Math.ExpDecay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证指数衰减阻尼的半衰期理论值准确性、多帧率步进一致性与极值守卫
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameMathExpDecayTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const float HalfLife = 0.5f;
    const float Initial = 0.0f;
    const float Target = 100.0f;

    const float OneHalfLifeVal = FGameMath::ExpDecayFloat(Initial, Target, HalfLife, HalfLife);
    TestTrue(TEXT("经过恰好 1 个半衰期时误差恰好缩小 50%（到达 50）"), FMath::IsNearlyEqual(OneHalfLifeVal, 50.0f, 0.01f));

    const float TwoHalfLifeVal = FGameMath::ExpDecayFloat(Initial, Target, HalfLife, HalfLife * 2.0f);
    TestTrue(TEXT("经过 2 个半衰期时到达 75"), FMath::IsNearlyEqual(TwoHalfLifeVal, 75.0f, 0.01f));

    float FastTickVal = Initial;
    for (int32 Step = 0; Step < 120; ++Step) {
        FastTickVal = FGameMath::ExpDecayFloat(FastTickVal, Target, HalfLife, 1.0f / 120.0f);
    }

    float SlowTickVal = Initial;
    for (int32 Step = 0; Step < 30; ++Step) {
        SlowTickVal = FGameMath::ExpDecayFloat(SlowTickVal, Target, HalfLife, 1.0f / 30.0f);
    }
    TestTrue(TEXT("120Hz 与 30Hz 累计 1 秒后结果在数学上精确一致"), FMath::IsNearlyEqual(FastTickVal, SlowTickVal, 0.01f));

    const float ZeroDtVal = FGameMath::ExpDecayFloat(42.0f, 100.0f, HalfLife, 0.0f);
    TestEqual(TEXT("DeltaTime 为 0 时保持原值不变"), ZeroDtVal, 42.0f);

    const float InstantVal = FGameMath::ExpDecayFloat(10.0f, 100.0f, 0.0f, 0.016f);
    TestEqual(TEXT("HalfLife 为 0 时瞬时收敛至目标值"), InstantVal, 100.0f);

    const FVector VecInitial(0.0, 0.0, 0.0);
    const FVector VecTarget(100.0, 200.0, 300.0);
    const FVector VecHalf = FGameMath::ExpDecayVector(VecInitial, VecTarget, HalfLife, HalfLife);
    TestTrue(TEXT("向量各分量在 1 个半衰期精准到达中点"), VecHalf.Equals(FVector(50.0, 100.0, 150.0), 0.1));

    const FQuat QuatA = FQuat::Identity;
    const FQuat QuatB = FQuat(FVector::UpVector, FMath::DegreesToRadians(90.0f));
    const FQuat QuatHalf = FGameMath::ExpDecayQuat(QuatA, QuatB, HalfLife, HalfLife);
    const FQuat QuatExpected = FQuat(FVector::UpVector, FMath::DegreesToRadians(45.0f));
    TestTrue(TEXT("四元数在 1 个半衰期平滑旋转 45 度"), QuatHalf.Equals(QuatExpected, 0.02f));

    const float AngleHalf = FGameMath::ExpDecayAngleDegrees(170.0f, -170.0f, HalfLife, HalfLife);
    TestTrue(TEXT("角度在 1 个半衰期沿最短弧度旋转到达 180 度附近"), FMath::IsNearlyEqual(FMath::Abs(AngleHalf), 180.0f, 0.1f));

    return true;
}

#endif
