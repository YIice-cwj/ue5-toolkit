/**
 * @file TRewindSnapshotBufferTest.cpp
 * @brief TRewindSnapshotBuffer 单元测试
 */

#include "Determinism/TRewindSnapshotBuffer.h"

#if WITH_AUTOMATION_TESTS

#include <limits>

#include "Misc/AutomationTest.h"

namespace {

struct FSimpleMotionState {
    FVector Position = FVector::ZeroVector;
    float Health = 100.0f;

    static FSimpleMotionState Lerp(const FSimpleMotionState& A, const FSimpleMotionState& B, float Alpha) {
        FSimpleMotionState Result;
        Result.Position = FMath::Lerp(A.Position, B.Position, Alpha);
        Result.Health = FMath::Lerp(A.Health, B.Health, Alpha);
        return Result;
    }
};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TRewindSnapshotBufferBasicAndInterpolationTest,
    "Game.Core.Determinism.RewindSnapshotBuffer.BasicAndInterpolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证定长历史快照环的存储回绕、精确帧查询、边界时间戳与亚帧插值
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool TRewindSnapshotBufferBasicAndInterpolationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TRewindSnapshotBuffer<FSimpleMotionState, 4> Buffer;

    TestTrue(TEXT("初始状态为空"), Buffer.IsEmpty());
    TestEqual(TEXT("初始数量为 0"), Buffer.Num(), 0);
    TestEqual(TEXT("容量配置正确"), Buffer.GetCapacity(), 4U);

    FSimpleMotionState Dummy;
    TestFalse(TEXT("空缓冲区查询最新失败"), Buffer.GetLatestSnapshot(Dummy));
    TestNull(TEXT("空缓冲区 PeekLatestSnapshot 返回 nullptr"), Buffer.PeekLatestSnapshot());
    TestFalse(TEXT("空缓冲区查询最早失败"), Buffer.GetOldestSnapshot(Dummy));
    TestNull(TEXT("空缓冲区 PeekOldestSnapshot 返回 nullptr"), Buffer.PeekOldestSnapshot());
    TestFalse(TEXT("空缓冲区精确帧查询失败"), Buffer.GetSnapshotAtFrame(1ULL, Dummy));
    TestNull(TEXT("空缓冲区 PeekSnapshotAtFrame 返回 nullptr"), Buffer.PeekSnapshotAtFrame(1ULL));
    TestFalse(TEXT("空缓冲区插值查询失败"), Buffer.GetInterpolatedSnapshot(1.0, Dummy, &FSimpleMotionState::Lerp));

    FSimpleMotionState S1;
    S1.Position = FVector(0.0f, 0.0f, 0.0f);
    S1.Health = 100.0f;
    Buffer.RecordSnapshot(1.0, 10ULL, S1);

    TestFalse(TEXT("记录一条后非空"), Buffer.IsEmpty());
    TestEqual(TEXT("数量为 1"), Buffer.Num(), 1);

    FSimpleMotionState SingleInterpolated;
    TestTrue(TEXT("单条快照插值成功"), Buffer.GetInterpolatedSnapshot(0.5, SingleInterpolated, &FSimpleMotionState::Lerp));
    TestEqual(TEXT("单条插值取唯一值"), SingleInterpolated.Health, 100.0f);

    FSimpleMotionState S2;
    S2.Position = FVector(100.0f, 0.0f, 0.0f);
    S2.Health = 90.0f;
    Buffer.RecordSnapshot(2.0, 11ULL, S2);

    FSimpleMotionState S3;
    S3.Position = FVector(200.0f, 0.0f, 0.0f);
    S3.Health = 80.0f;
    Buffer.RecordSnapshot(3.0, 12ULL, S3);

    FSimpleMotionState S4;
    S4.Position = FVector(300.0f, 0.0f, 0.0f);
    S4.Health = 70.0f;
    Buffer.RecordSnapshot(4.0, 13ULL, S4);

    TestEqual(TEXT("填满容量后数量为 4"), Buffer.Num(), 4);

    FSimpleMotionState Latest;
    TestTrue(TEXT("成功获取最新快照"), Buffer.GetLatestSnapshot(Latest));
    TestEqual(TEXT("最新快照血量正确"), Latest.Health, 70.0f);
    TestNotNull(TEXT("PeekLatestSnapshot 返回有效指针"), Buffer.PeekLatestSnapshot());
    TestEqual(TEXT("PeekLatestSnapshot 字段正确"), Buffer.PeekLatestSnapshot()->Health, 70.0f);

    FSimpleMotionState Oldest;
    TestTrue(TEXT("成功获取最早快照"), Buffer.GetOldestSnapshot(Oldest));
    TestEqual(TEXT("最早快照血量正确"), Oldest.Health, 100.0f);
    TestNotNull(TEXT("PeekOldestSnapshot 返回有效指针"), Buffer.PeekOldestSnapshot());
    TestEqual(TEXT("PeekOldestSnapshot 字段正确"), Buffer.PeekOldestSnapshot()->Health, 100.0f);

    FSimpleMotionState FoundFrame;
    TestTrue(TEXT("找到帧 11"), Buffer.GetSnapshotAtFrame(11ULL, FoundFrame));
    TestEqual(TEXT("帧 11 数据匹配"), FoundFrame.Position.X, 100.0);
    TestNotNull(TEXT("PeekSnapshotAtFrame 找到帧 11"), Buffer.PeekSnapshotAtFrame(11ULL));
    TestEqual(TEXT("PeekSnapshotAtFrame 帧 11 数据匹配"), Buffer.PeekSnapshotAtFrame(11ULL)->Position.X, 100.0);
    TestFalse(TEXT("未找到不存在的帧 99"), Buffer.GetSnapshotAtFrame(99ULL, FoundFrame));
    TestNull(TEXT("PeekSnapshotAtFrame 未找到帧 99 返回 nullptr"), Buffer.PeekSnapshotAtFrame(99ULL));

    FSimpleMotionState S5;
    S5.Position = FVector(400.0f, 0.0f, 0.0f);
    S5.Health = 60.0f;
    Buffer.RecordSnapshot(5.0, 14ULL, S5);

    TestEqual(TEXT("环形回绕后数量保持为 4"), Buffer.Num(), 4);
    TestFalse(TEXT("最早的帧 10 已被安全覆盖"), Buffer.GetSnapshotAtFrame(10ULL, FoundFrame));

    TestTrue(TEXT("回绕后成功获取新最早快照"), Buffer.GetOldestSnapshot(Oldest));
    TestEqual(TEXT("新最早快照为帧 11"), Oldest.Health, 90.0f);

    FSimpleMotionState InterpPast;
    TestTrue(TEXT("早于历史最早时间戳插值成功"), Buffer.GetInterpolatedSnapshot(0.0, InterpPast, &FSimpleMotionState::Lerp));
    TestEqual(TEXT("早于历史插值钳制到最早快照"), InterpPast.Health, 90.0f);

    FSimpleMotionState InterpFuture;
    TestTrue(TEXT("晚于最新时间戳插值成功"), Buffer.GetInterpolatedSnapshot(10.0, InterpFuture, &FSimpleMotionState::Lerp));
    TestEqual(TEXT("晚于最新插值钳制到最新快照"), InterpFuture.Health, 60.0f);

    FSimpleMotionState MidInterp;
    TestTrue(TEXT("亚帧中值插值成功"), Buffer.GetInterpolatedSnapshot(3.5, MidInterp, &FSimpleMotionState::Lerp));
    TestEqual(TEXT("中值坐标精确插值"), MidInterp.Position.X, 250.0);
    TestEqual(TEXT("中值血量精确插值"), MidInterp.Health, 75.0f);

    Buffer.Clear();
    TestTrue(TEXT("清空后为空"), Buffer.IsEmpty());
    TestEqual(TEXT("清空后数量为 0"), Buffer.Num(), 0);

    Buffer.RecordSnapshot(1.0, 100ULL, S1);
    Buffer.RecordSnapshot(2.0, 101ULL, S2);
    Buffer.RecordSnapshot(1.5, 102ULL, S3);
    FSimpleMotionState OutOfOrderInterp;
    TestTrue(
        TEXT("乱序较小时间戳被自动钳制维持单调性后插值查询正常命中"),
        Buffer.GetInterpolatedSnapshot(1.5, OutOfOrderInterp, &FSimpleMotionState::Lerp)
    );
    TestEqual(TEXT("1.5s 处于 [1.0, 2.0] 中点插值血量为 95"), OutOfOrderInterp.Health, 95.0f);

    FSimpleMotionState InvalidNanInterp;
    const double NanTimestamp = std::numeric_limits<double>::quiet_NaN();
    TestFalse(
        TEXT("传入非有限时间戳 (NaN) 无匹配区间时 GetInterpolatedSnapshot 安全返回 false"),
        Buffer.GetInterpolatedSnapshot(NanTimestamp, InvalidNanInterp, &FSimpleMotionState::Lerp)
    );

    Buffer.Clear();
    FSimpleMotionState MoveTarget;
    MoveTarget.Position = FVector(777.0f, 888.0f, 999.0f);
    MoveTarget.Health = 55.0f;
    Buffer.RecordSnapshot(1.0, 10ULL, MoveTemp(MoveTarget));
    TestEqual(TEXT("移动写入快照成功"), Buffer.PeekLatestSnapshot()->Health, 55.0f);

    Buffer.Clear();
    FSimpleMotionState Jump1;
    Jump1.Position = FVector(10.0f, 0.0f, 0.0f);
    Buffer.RecordSnapshot(1.0, 10ULL, Jump1);

    FSimpleMotionState Jump2;
    Jump2.Position = FVector(50.0f, 0.0f, 0.0f);
    Buffer.RecordSnapshot(2.0, 50ULL, Jump2);

    FSimpleMotionState Jump3;
    Jump3.Position = FVector(90.0f, 0.0f, 0.0f);
    Buffer.RecordSnapshot(3.0, 90ULL, Jump3);

    FSimpleMotionState NonSequentialFound;
    TestTrue(TEXT("非连续跳跃帧回退环形扫描命中帧 50"), Buffer.GetSnapshotAtFrame(50ULL, NonSequentialFound));
    TestEqual(TEXT("非连续帧数据精确匹配"), NonSequentialFound.Position.X, 50.0);
    TestNotNull(TEXT("非连续跳跃帧 PeekSnapshotAtFrame 命中"), Buffer.PeekSnapshotAtFrame(50ULL));
    TestEqual(TEXT("非连续跳跃帧 Peek 数据精确匹配"), Buffer.PeekSnapshotAtFrame(50ULL)->Position.X, 50.0);
    TestFalse(TEXT("非连续跳跃帧未记录帧查询返回 false"), Buffer.GetSnapshotAtFrame(30ULL, NonSequentialFound));
    TestNull(TEXT("非连续跳跃帧未记录帧 Peek 返回 nullptr"), Buffer.PeekSnapshotAtFrame(30ULL));

    Buffer.Clear();
    FSimpleMotionState SameTime1;
    SameTime1.Health = 10.0f;
    Buffer.RecordSnapshot(5.0, 1ULL, SameTime1);

    FSimpleMotionState SameTime2;
    SameTime2.Health = 20.0f;
    Buffer.RecordSnapshot(5.0, 2ULL, SameTime2);

    FSimpleMotionState ZeroDeltaInterp;
    TestTrue(TEXT("零时差多帧插值安全命中"), Buffer.GetInterpolatedSnapshot(5.0, ZeroDeltaInterp, &FSimpleMotionState::Lerp));
    TestEqual(TEXT("零时差插值安全返回最新帧"), ZeroDeltaInterp.Health, 20.0f);

    Buffer.Clear();
    Buffer.EmplaceSnapshot(10.0, 100ULL, FVector(11.0f, 22.0f, 33.0f), 88.0f);
    TestEqual(TEXT("EmplaceSnapshot 原位构造成功"), Buffer.Num(), 1);

    TOptional<FSimpleMotionState> OptLatest = Buffer.GetLatestSnapshot();
    TestTrue(TEXT("GetLatestSnapshot Optional 有效"), OptLatest.IsSet());
    if (OptLatest.IsSet()) {
        TestEqual(TEXT("Optional 最新状态字段正确"), OptLatest->Health, 88.0f);
        TestEqual(TEXT("Optional 最新坐标 X 正确"), OptLatest->Position.X, 11.0);
    }

    TOptional<FSimpleMotionState> OptOldest = Buffer.GetOldestSnapshot();
    TestTrue(TEXT("GetOldestSnapshot Optional 有效"), OptOldest.IsSet());
    if (OptOldest.IsSet()) {
        TestEqual(TEXT("Optional 最早状态字段正确"), OptOldest->Health, 88.0f);
    }

    TOptional<FSimpleMotionState> OptFrame = Buffer.GetSnapshotAtFrame(100ULL);
    TestTrue(TEXT("GetSnapshotAtFrame Optional 命中"), OptFrame.IsSet());
    if (OptFrame.IsSet()) {
        TestEqual(TEXT("Optional 帧状态匹配"), OptFrame->Health, 88.0f);
    }
    TestFalse(TEXT("GetSnapshotAtFrame Optional 未命中返回空"), Buffer.GetSnapshotAtFrame(999ULL).IsSet());

    Buffer.EmplaceSnapshot(20.0, 101ULL, FVector(111.0f, 222.0f, 333.0f), 10.0f);
    TOptional<FSimpleMotionState> OptInterp = Buffer.GetInterpolatedSnapshot(15.0, &FSimpleMotionState::Lerp);
    TestTrue(TEXT("GetInterpolatedSnapshot Optional 成功计算"), OptInterp.IsSet());
    if (OptInterp.IsSet()) {
        TestEqual(TEXT("Optional 插值血量正确"), OptInterp->Health, 49.0f);
    }

    return true;
}

#endif
