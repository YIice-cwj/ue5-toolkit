// Copyright Epic Games, Inc. All Rights Reserved.

#include "Async/Async.h"
#include "Misc/AutomationTest.h"
#include "Remote/GameRemoteLogOutputDevice.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameRemoteLogOutputDeviceTest,
    "Game.Logging.RemoteOutputDevice.BasicFilteringAndConcurrency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行远程日志输出设备过滤、容量、重入保护与并发压测
 * @param Parameters 测试参数
 * @return 全部断言通过返回 true
 */
bool FGameRemoteLogOutputDeviceTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameRemoteLogOutputDevice Device;

    TestEqual(TEXT("默认最低采集级别应为 Warning"), Device.GetMinVerbosity(), ELogVerbosity::Warning);
    TestTrue(TEXT("应声明支持跨多线程调用"), Device.CanBeUsedOnMultipleThreads());
    TestEqual(TEXT("初始队列数量应为 0"), Device.GetQueuedCount(), 0);
    TestEqual(TEXT("初始丢弃数量应为 0"), Device.GetDroppedCount(), int64(0));

    Device.Serialize(TEXT("Info message"), ELogVerbosity::Log, FName(TEXT("LogTest")));
    TestEqual(TEXT("低于 Warning 的日志应被过滤忽略"), Device.GetQueuedCount(), 0);

    Device.Serialize(TEXT("Warning message"), ELogVerbosity::Warning, FName(TEXT("LogTest")), 100.5);
    TestEqual(TEXT("Warning 级别日志应成功入队"), Device.GetQueuedCount(), 1);

    Device.Serialize(TEXT("Error message"), ELogVerbosity::Error, FName(TEXT("LogTest")), 101.0);
    TestEqual(TEXT("Error 级别日志应成功入队"), Device.GetQueuedCount(), 2);

    Device.SetMinVerbosity(ELogVerbosity::Log);
    TestEqual(TEXT("最低采集级别应已更新为 Log"), Device.GetMinVerbosity(), ELogVerbosity::Log);

    Device.Serialize(TEXT("Log message after threshold change"), ELogVerbosity::Log, FName(TEXT("LogTest")), 102.0);
    TestEqual(TEXT("门槛放宽后 Log 级别应成功入队"), Device.GetQueuedCount(), 3);

    TArray<FGameRemoteLogEntry> PartialBatch;
    const int32 DrainedTwo = Device.DrainBatch(PartialBatch, 2);
    TestEqual(TEXT("首次应排出指定上限 2 条"), DrainedTwo, 2);
    TestEqual(TEXT("队列中应残留 1 条"), Device.GetQueuedCount(), 1);
    TestEqual(TEXT("批次首项文本校验"), PartialBatch[0].Message, FString(TEXT("Warning message")));
    TestEqual(TEXT("批次首项分类校验"), PartialBatch[0].Category, FName(TEXT("LogTest")));
    TestEqual(TEXT("批次首项级别校验"), PartialBatch[0].GetVerbosity(), ELogVerbosity::Warning);
    TestEqual(TEXT("批次首项时间戳校验"), PartialBatch[0].Timestamp, 100.5);

    const FName FilteredCat(TEXT("IgnoredCategory"));
    Device.AddIgnoredCategory(FilteredCat);
    Device.Serialize(TEXT("Should be ignored"), ELogVerbosity::Error, FilteredCat);
    TestEqual(TEXT("黑名单分类日志应被忽略"), Device.GetQueuedCount(), 1);

    Device.RemoveIgnoredCategory(FilteredCat);
    Device.Serialize(TEXT("Should pass after removal"), ELogVerbosity::Error, FilteredCat);
    TestEqual(TEXT("移除黑名单后该分类日志应放行入队"), Device.GetQueuedCount(), 2);

    Device.Clear();
    TestEqual(TEXT("清空后队列数量应为 0"), Device.GetQueuedCount(), 0);
    TestEqual(TEXT("清空后丢弃数量应为 0"), Device.GetDroppedCount(), int64(0));

    Device.SetMaxCapacity(3);
    Device.Serialize(TEXT("Msg 1"), ELogVerbosity::Error, FName(TEXT("LogTest")));
    Device.Serialize(TEXT("Msg 2"), ELogVerbosity::Error, FName(TEXT("LogTest")));
    Device.Serialize(TEXT("Msg 3"), ELogVerbosity::Error, FName(TEXT("LogTest")));
    TestEqual(TEXT("队列应达到满额容量 3"), Device.GetQueuedCount(), 3);
    TestEqual(TEXT("未满前丢弃数应为 0"), Device.GetDroppedCount(), int64(0));

    Device.Serialize(TEXT("Msg 4 Overflow"), ELogVerbosity::Error, FName(TEXT("LogTest")));
    TestEqual(TEXT("溢出后队列容量应维持 3"), Device.GetQueuedCount(), 3);
    TestEqual(TEXT("溢出日志应计入丢弃计数器"), Device.GetDroppedCount(), int64(1));

    Device.Clear();
    Device.SetMaxCapacity(1000);
    constexpr int32 ThreadCount = 4;
    constexpr int32 LogsPerThread = 50;
    TArray<TFuture<void>> Futures;
    Futures.Reserve(ThreadCount);

    for (int32 ThreadIdx = 0; ThreadIdx < ThreadCount; ++ThreadIdx) {
        Futures.Add(Async(EAsyncExecution::Thread, [&Device, ThreadIdx]() {
            for (int32 LogIdx = 0; LogIdx < LogsPerThread; ++LogIdx) {
                const FString Msg = FString::Printf(TEXT("T%d_L%d"), ThreadIdx, LogIdx);
                Device.Serialize(*Msg, ELogVerbosity::Error, FName(TEXT("ConcurrentLog")));
            }
        }));
    }

    for (TFuture<void>& Fut : Futures) {
        Fut.Get();
    }

    TestEqual(
        TEXT("多工作线程并发压测入队总数应完全一致"),
        Device.GetQueuedCount(),
        ThreadCount * LogsPerThread
    );

    TArray<FGameRemoteLogEntry> FullBatch;
    const int32 TotalDrained = Device.DrainBatch(FullBatch, ThreadCount * LogsPerThread + 10);
    TestEqual(TEXT("主线程应完整排空并发压测条目"), TotalDrained, ThreadCount * LogsPerThread);
    TestEqual(TEXT("排空后队列应为 0"), Device.GetQueuedCount(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameRemoteLogOutputDeviceCoWAndContentionTest,
    "Game.Logging.RemoteOutputDevice.CoWAndContention",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多线程并发进行写日志与动态修改忽略分类名单时，COW 无锁快照的安全与准确性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameRemoteLogOutputDeviceCoWAndContentionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameRemoteLogOutputDevice Device;
    Device.SetMaxCapacity(10000);
    Device.SetMinVerbosity(ELogVerbosity::Warning);

    const FName DynamicCategory(TEXT("LogDynamicFlakyCat"));
    constexpr int32 WorkerCount = 8;
    constexpr int32 IterationsPerWorker = 200;

    std::atomic<bool> bKeepMutating{true};
    TFuture<void> MutatorFuture = Async(EAsyncExecution::Thread, [&Device, &bKeepMutating, DynamicCategory]() {
        while (bKeepMutating.load(std::memory_order_relaxed)) {
            Device.AddIgnoredCategory(DynamicCategory);
            Device.RemoveIgnoredCategory(DynamicCategory);
        }
    });

    TArray<TFuture<void>> WorkerFutures;
    WorkerFutures.Reserve(WorkerCount);
    for (int32 WorkerIdx = 0; WorkerIdx < WorkerCount; ++WorkerIdx) {
        WorkerFutures.Add(Async(EAsyncExecution::Thread, [&Device, DynamicCategory]() {
            for (int32 Step = 0; Step < IterationsPerWorker; ++Step) {
                const FName Cat = (Step % 2 == 0) ? DynamicCategory : FName(TEXT("LogAlwaysAllowedCat"));
                Device.Serialize(TEXT("StressMessage"), ELogVerbosity::Warning, Cat);
            }
        }));
    }

    for (TFuture<void>& Fut : WorkerFutures) {
        Fut.Get();
    }
    bKeepMutating.store(false, std::memory_order_relaxed);
    MutatorFuture.Get();

    const int32 TotalQueued = Device.GetQueuedCount();
    const int32 MinGuaranteed = WorkerCount * (IterationsPerWorker / 2);
    TestTrue(
        FString::Printf(TEXT("入队数量 (%d) 应至少包含确定放行的分类 (%d)"), TotalQueued, MinGuaranteed),
        TotalQueued >= MinGuaranteed
    );

    TArray<FGameRemoteLogEntry> Drained;
    const int32 ActuallyDrained = Device.DrainBatch(Drained, TotalQueued + 100);
    TestEqual(TEXT("排空数量应与队列记录严格吻合"), ActuallyDrained, TotalQueued);
    TestEqual(TEXT("排空后队列条目为 0"), Device.GetQueuedCount(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameRemoteLogOutputDeviceBatchAtomicDrainTest,
    "Game.Logging.RemoteOutputDevice.BatchAtomicDrain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证大批次排出时计数器单次合并扣减与状态平滑一致性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameRemoteLogOutputDeviceBatchAtomicDrainTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameRemoteLogOutputDevice Device;
    Device.SetMaxCapacity(500);

    for (int32 Index = 0; Index < 200; ++Index) {
        Device.Serialize(TEXT("BatchItem"), ELogVerbosity::Error, FName(TEXT("LogBatchCat")));
    }
    TestEqual(TEXT("初始入队 200 条"), Device.GetQueuedCount(), 200);

    TArray<FGameRemoteLogEntry> Batch1;
    const int32 Drained1 = Device.DrainBatch(Batch1, 50);
    TestEqual(TEXT("分批排出第 1 批 50 条"), Drained1, 50);
    TestEqual(TEXT("剩余队列计数应精确等于 150"), Device.GetQueuedCount(), 150);

    TArray<FGameRemoteLogEntry> Batch2;
    const int32 Drained2 = Device.DrainBatch(Batch2, 200);
    TestEqual(TEXT("排空剩余全部条目"), Drained2, 150);
    TestEqual(TEXT("剩余队列计数应为 0"), Device.GetQueuedCount(), 0);

    Device.Clear();
    TestEqual(TEXT("多次 Clear 不会产生负数或状态错乱"), Device.GetQueuedCount(), 0);

    return true;
}

#endif // WITH_AUTOMATION_TESTS
