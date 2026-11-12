/**
 * @file FStateChecksumTest.cpp
 * @brief FStateChecksumBuilder 与 FDesyncDetector 单元测试
 */

#include "Determinism/FStateChecksumBuilder.h"
#include "Determinism/FDesyncDetector.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FStateChecksumBuilderDeterminismTest,
    "Game.Core.Determinism.StateChecksumBuilder.DeterminismAndAvalanche",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证确定性状态校验器的跨实例确定性、量化容差、雪崩效应与边缘防御
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FStateChecksumBuilderDeterminismTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FStateChecksumBuilder BuilderA;
    FStateChecksumBuilder BuilderB;

    TestEqual(TEXT("初始状态哈希完全一致"), BuilderA.Finalize(), BuilderB.Finalize());

    BuilderA.AddBytes(nullptr, 0);
    BuilderA.AddBytes(nullptr, 10);
    TestEqual(TEXT("注入空字节流不改变哈希"), BuilderA.Finalize(), BuilderB.Finalize());

    BuilderA.AddUInt32(42);
    BuilderA.AddUInt64(10086ULL);
    BuilderA.AddBool(true);
    BuilderA.AddInt32(-123);
    BuilderA.AddInt64(-9876543210LL);
    BuilderA.AddString(TEXT("PlayerState_Hero"));
    BuilderA.AddName(FName(TEXT("StaticMesh_Sword")));
    BuilderA.AddFloatQuantized(12.345f, 0.001f);
    BuilderA.AddVectorQuantized(FVector(10.0, 20.0, 30.0), 0.01f);

    const uint8 RawPayload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    BuilderA.AddBytes(RawPayload, sizeof(RawPayload));

    BuilderB.AddUInt32(42);
    BuilderB.AddUInt64(10086ULL);
    BuilderB.AddBool(true);
    BuilderB.AddInt32(-123);
    BuilderB.AddInt64(-9876543210LL);
    BuilderB.AddString(TEXT("PlayerState_Hero"));
    BuilderB.AddName(FName(TEXT("StaticMesh_Sword")));
    BuilderB.AddFloatQuantized(12.345f, 0.001f);
    BuilderB.AddVectorQuantized(FVector(10.0, 20.0, 30.0), 0.01f);
    BuilderB.AddBytes(RawPayload, sizeof(RawPayload));

    const uint64 HashA = BuilderA.Finalize();
    const uint64 HashB = BuilderB.Finalize();
    TestEqual(TEXT("相同输入流的哈希码 100% 确定性一致"), HashA, HashB);

    FStateChecksumBuilder BuilderJitter;
    BuilderJitter.AddUInt32(42);
    BuilderJitter.AddUInt64(10086ULL);
    BuilderJitter.AddBool(true);
    BuilderJitter.AddInt32(-123);
    BuilderJitter.AddInt64(-9876543210LL);
    BuilderJitter.AddString(TEXT("PlayerState_Hero"));
    BuilderJitter.AddName(FName(TEXT("StaticMesh_Sword")));
    BuilderJitter.AddFloatQuantized(12.34502f, 0.001f);
    BuilderJitter.AddVectorQuantized(FVector(10.001, 20.002, 30.001), 0.01f);
    BuilderJitter.AddBytes(RawPayload, sizeof(RawPayload));

    TestEqual(TEXT("精度容差内的微小抖动被平滑量化消除"), BuilderJitter.Finalize(), HashA);

    FStateChecksumBuilder BuilderDiverged;
    BuilderDiverged.AddUInt32(42);
    BuilderDiverged.AddUInt64(10086ULL);
    BuilderDiverged.AddFloatQuantized(12.500f, 0.001f);
    BuilderDiverged.AddVectorQuantized(FVector(10.0, 20.0, 30.0), 0.01f);
    BuilderDiverged.AddBytes(RawPayload, sizeof(RawPayload));

    TestNotEqual(TEXT("超差输入产生明显哈希雪崩"), BuilderDiverged.Finalize(), HashA);

    BuilderA.Reset();
    FStateChecksumBuilder EmptyBuilder;
    TestEqual(TEXT("Reset 后恢复初始哈希"), BuilderA.Finalize(), EmptyBuilder.Finalize());

    FStateChecksumBuilder BuilderNaN1;
    BuilderNaN1.AddFloatQuantized(NAN, 0.001f);
    BuilderNaN1.AddVectorQuantized(FVector(NAN, INFINITY, -INFINITY), 0.01f);

    FStateChecksumBuilder BuilderNaN2;
    BuilderNaN2.AddFloatQuantized(NAN, 0.001f);
    BuilderNaN2.AddVectorQuantized(FVector(NAN, INFINITY, -INFINITY), 0.01f);

    FStateChecksumBuilder BuilderZero;
    BuilderZero.AddFloatQuantized(0.0f, 0.001f);
    BuilderZero.AddVectorQuantized(FVector(0.0, 0.0, 0.0), 0.01f);

    TestEqual(TEXT("非法浮点（NAN/INF）输入具备一致的哈希结果且不崩溃"), BuilderNaN1.Finalize(), BuilderNaN2.Finalize());
    TestNotEqual(TEXT("非法浮点输入绝不与正常 0.0 哈希碰撞产生伪同步"), BuilderNaN1.Finalize(), BuilderZero.Finalize());

    FStateChecksumBuilder BuilderLwcA;
    BuilderLwcA.AddDoubleQuantized(5000000.12345, 0.001);
    BuilderLwcA.AddVectorQuantized(FVector(1000000.0, 2000000.0, 3000000.0), 0.01);

    FStateChecksumBuilder BuilderLwcJitter;
    BuilderLwcJitter.AddDoubleQuantized(5000000.12349, 0.001);
    BuilderLwcJitter.AddVectorQuantized(FVector(1000000.002, 2000000.001, 3000000.002), 0.01);

    TestEqual(TEXT("大世界双精度坐标在容差范围内平滑量化消除抖动"), BuilderLwcJitter.Finalize(), BuilderLwcA.Finalize());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDesyncDetectorDetectionTest,
    "Game.Core.Determinism.DesyncDetector.DetectionAndCallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证双端分叉探测器的匹配通过、不同步中断报警、回调触发与回绕覆盖
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FDesyncDetectorDetectionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FDesyncDetector Detector;
    TestEqual(TEXT("GetCapacity 返回 128"), Detector.GetCapacity(), FDesyncDetector::HistoryCapacity);
    TestFalse(TEXT("初始状态 HasFrame 返回 false"), Detector.HasFrame(10ULL));

    uint64 QueriedChecksum = 0ULL;
    TestFalse(TEXT("初始状态查询任意帧返回 false"), Detector.GetLocalChecksum(10ULL, QueriedChecksum));

    FDesyncReport Report;
    TestFalse(TEXT("未记录帧校验返回 false 且未分叉"), Detector.VerifyRemoteChecksum(10ULL, 12345ULL, Report));
    TestFalse(TEXT("报告标记为未分叉"), Report.bDiverged);

    Detector.RecordLocalChecksum(10ULL, 0xAABBCCDDEEFFULL);
    TestTrue(TEXT("记录后 HasFrame 返回 true"), Detector.HasFrame(10ULL));
    TestTrue(TEXT("成功查询已记录的本地帧"), Detector.GetLocalChecksum(10ULL, QueriedChecksum));
    TestEqual(TEXT("查询校验码一致"), QueriedChecksum, 0xAABBCCDDEEFFULL);

    TestTrue(TEXT("远端校验码匹配返回 true"), Detector.VerifyRemoteChecksum(10ULL, 0xAABBCCDDEEFFULL, Report));
    TestFalse(TEXT("匹配报告标记未分叉"), Report.bDiverged);
    TestEqual(TEXT("报告帧序号正确"), Report.Frame, 10ULL);

    int32 DesyncCallbackCount = 0;
    FDesyncReport CallbackReport;
    Detector.SetDesyncCallback(FDesyncDetector::FOnDesyncDetected::CreateLambda(
        [&DesyncCallbackCount, &CallbackReport](const FDesyncReport& InReport) {
            ++DesyncCallbackCount;
            CallbackReport = InReport;
        }
    ));

    TestFalse(TEXT("校验码不同步校验返回 false"), Detector.VerifyRemoteChecksum(10ULL, 0x112233445566ULL, Report));
    TestTrue(TEXT("报告标记已分叉"), Report.bDiverged);
    TestEqual(TEXT("分叉回调已触发"), DesyncCallbackCount, 1);
    TestEqual(TEXT("回调报告帧序号匹配"), CallbackReport.Frame, 10ULL);
    TestEqual(TEXT("回调报告本地码正确"), CallbackReport.LocalChecksum, 0xAABBCCDDEEFFULL);
    TestEqual(TEXT("回调报告远端码正确"), CallbackReport.RemoteChecksum, 0x112233445566ULL);

    Detector.RecordLocalChecksum(10ULL + FDesyncDetector::HistoryCapacity, 0x9999ULL);
    TestTrue(TEXT("新回绕帧记录成功"), Detector.GetLocalChecksum(10ULL + FDesyncDetector::HistoryCapacity, QueriedChecksum));
    TestEqual(TEXT("新回绕帧校验码匹配"), QueriedChecksum, 0x9999ULL);
    TestFalse(TEXT("旧帧已被回绕覆盖"), Detector.GetLocalChecksum(10ULL, QueriedChecksum));

    Detector.Reset();
    TestFalse(TEXT("Reset 后所有槽位失效"), Detector.GetLocalChecksum(10ULL + FDesyncDetector::HistoryCapacity, QueriedChecksum));

    return true;
}

#endif
