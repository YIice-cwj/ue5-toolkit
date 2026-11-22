// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameRemoteLogOutputDeviceTest.cpp
 * @brief FGameRemoteLogOutputDevice 过滤、SetColor/NoLogging 拦截、重入守卫、严格容量上限与高并发 MPMC 压测
 */

#include "Remote/GameRemoteLogOutputDevice.h"

#if WITH_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Misc/AutomationTest.h"
#include "Remote/FRemoteLogReentrancyScope.h"

#include <atomic>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameRemoteLogOutputDeviceFilteringAndCapacityTest,
    "Game.Logging.RemoteOutputDevice.FilteringAndCapacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证级别阈值、SetColor/NoLogging 拦截、黑名单管理、容量钳制、延迟时间戳与 DiscardBatch/Clear 行为
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameRemoteLogOutputDeviceFilteringAndCapacityTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameRemoteLogOutputDevice Device;

    TestEqual(TEXT("默认最低采集级别应为 Warning"), Device.GetMinVerbosity(), ELogVerbosity::Warning);
    TestEqual(TEXT("默认容量上限应等于 MAX_CAPACITY"), Device.GetMaxCapacity(), FGameRemoteLogOutputDevice::MAX_CAPACITY);
    TestTrue(TEXT("应声明支持跨多线程调用"), Device.CanBeUsedOnMultipleThreads());
    TestTrue(TEXT("初始队列应为空"), Device.IsEmpty());
    TestEqual(TEXT("初始队列数量应为 0"), Device.GetQueuedCount(), 0);
    TestEqual(TEXT("初始丢弃数量应为 0"), Device.GetDroppedCount(), int64(0));

    TestTrue(TEXT("默认黑名单应包含 LogGameMessage"), Device.IsCategoryIgnored(FName(TEXT("LogGameMessage"))));
    TestTrue(TEXT("默认黑名单应包含 LogGameNetService"), Device.IsCategoryIgnored(FName(TEXT("LogGameNetService"))));
    TestTrue(TEXT("默认黑名单应包含 LogGameNetCore"), Device.IsCategoryIgnored(FName(TEXT("LogGameNetCore"))));
    TestFalse(TEXT("NAME_None 不应被视为黑名单分类"), Device.IsCategoryIgnored(NAME_None));

    Device.Serialize(TEXT("1100"), ELogVerbosity::SetColor, NAME_None);
    Device.Serialize(TEXT("NoLoggingMsg"), ELogVerbosity::NoLogging, FName(TEXT("LogTest")));
    TestEqual(TEXT("SetColor 与 NoLogging 控制消息必须被彻底拦截"), Device.GetQueuedCount(), 0);

    Device.Serialize(TEXT("Info message"), ELogVerbosity::Log, FName(TEXT("LogTest")));
    TestEqual(TEXT("低于 Warning 的日志应被过滤忽略"), Device.GetQueuedCount(), 0);

    Device.Serialize(TEXT("Warning message"), ELogVerbosity::Warning, FName(TEXT("LogTest")), 100.5);
    TestEqual(TEXT("Warning 级别日志应成功入队"), Device.GetQueuedCount(), 1);
    TestFalse(TEXT("入队后 IsEmpty 应为 false"), Device.IsEmpty());

    Device.Serialize(nullptr, ELogVerbosity::Error, FName(TEXT("LogTest")), 0.0);
    TestEqual(TEXT("nullptr 正文与 0.0 时间戳应安全入队"), Device.GetQueuedCount(), 2);

    Device.SetMinVerbosity(ELogVerbosity::NoLogging);
    Device.Serialize(TEXT("Silenced error"), ELogVerbosity::Error, FName(TEXT("LogTest")));
    Device.Serialize(TEXT("Silenced color"), ELogVerbosity::SetColor, NAME_None);
    TestEqual(TEXT("设为 NoLogging 时应静默全部日志与控制消息"), Device.GetQueuedCount(), 2);

    Device.SetMinVerbosity(ELogVerbosity::Log);
    TestEqual(TEXT("最低采集级别应已更新为 Log"), Device.GetMinVerbosity(), ELogVerbosity::Log);

    Device.Serialize(TEXT("Log message auto timestamp"), ELogVerbosity::Log, FName(TEXT("LogTest")));
    TestEqual(TEXT("门槛放宽后 3 参 Log 级别应成功入队"), Device.GetQueuedCount(), 3);

    TArray<FGameRemoteLogEntry> EmptyDrain;
    TestEqual(TEXT("DrainBatch(0) 应返回 0"), Device.DrainBatch(EmptyDrain, 0), 0);
    TestEqual(TEXT("DrainBatch(-5) 应返回 0"), Device.DrainBatch(EmptyDrain, -5), 0);
    TestEqual(TEXT("DiscardBatch(0) 应返回 0"), Device.DiscardBatch(0), 0);

    TArray<FGameRemoteLogEntry> PartialBatch;
    const int32 DrainedTwo = Device.DrainBatch(PartialBatch, 2);
    TestEqual(TEXT("首次应排出指定上限 2 条"), DrainedTwo, 2);
    TestEqual(TEXT("队列中应残留 1 条"), Device.GetQueuedCount(), 1);
    TestEqual(TEXT("批次首项文本校验"), PartialBatch[0].Message, FString(TEXT("Warning message")));
    TestEqual(TEXT("批次首项分类校验"), PartialBatch[0].Category, FName(TEXT("LogTest")));
    TestEqual(TEXT("批次首项级别校验"), PartialBatch[0].GetVerbosity(), ELogVerbosity::Warning);
    TestEqual(TEXT("批次首项时间戳校验"), PartialBatch[0].Timestamp, 100.5);
    TestTrue(TEXT("nullptr 入队正文应转为空字符串"), PartialBatch[1].Message.IsEmpty());
    TestEqual(TEXT("显式 0.0 时间戳不得被覆盖"), PartialBatch[1].Timestamp, 0.0);

    TArray<FGameRemoteLogEntry> ThirdBatch;
    TestEqual(TEXT("排出第 3 条自动补全时间戳日志"), Device.DrainBatch(ThirdBatch, 4), 1);
    TestTrue(TEXT("3 参 Serialize 延迟补全的时间戳应大于 0"), ThirdBatch[0].Timestamp > 0.0);

    const FName FilteredCat(TEXT("IgnoredCategory"));
    Device.AddIgnoredCategory(NAME_None);
    Device.RemoveIgnoredCategory(NAME_None);
    Device.AddIgnoredCategory(FilteredCat);
    Device.AddIgnoredCategory(FilteredCat);
    TestTrue(TEXT("动态添加的分类应在黑名单中"), Device.IsCategoryIgnored(FilteredCat));
    Device.Serialize(TEXT("Should be ignored"), ELogVerbosity::Error, FilteredCat);
    TestEqual(TEXT("黑名单分类日志应被忽略"), Device.GetQueuedCount(), 0);

    Device.RemoveIgnoredCategory(FilteredCat);
    Device.RemoveIgnoredCategory(FilteredCat);
    TestFalse(TEXT("移除后分类不再位于黑名单中"), Device.IsCategoryIgnored(FilteredCat));
    Device.Serialize(TEXT("Should pass after removal"), ELogVerbosity::Error, FilteredCat);
    TestEqual(TEXT("移除黑名单后该分类日志应放行入队"), Device.GetQueuedCount(), 1);

    TestEqual(TEXT("DiscardBatch 应原地丢弃 1 条日志"), Device.DiscardBatch(5), 1);
    TestTrue(TEXT("DiscardBatch 后队列应为空"), Device.IsEmpty());
    TestEqual(TEXT("DiscardBatch 后队列计数为 0"), Device.GetQueuedCount(), 0);

    Device.SetMaxCapacity(0);
    Device.SetMaxCapacity(-10);
    TestEqual(TEXT("非正容量设置应被忽略"), Device.GetMaxCapacity(), FGameRemoteLogOutputDevice::MAX_CAPACITY);

    Device.SetMaxCapacity(FGameRemoteLogOutputDevice::MAX_CAPACITY * 4);
    TestEqual(TEXT("超额容量应被钳制至 MAX_CAPACITY"), Device.GetMaxCapacity(), FGameRemoteLogOutputDevice::MAX_CAPACITY);

    Device.SetMaxCapacity(3);
    TestEqual(TEXT("合法软上限应生效为 3"), Device.GetMaxCapacity(), 3);
    Device.Serialize(TEXT("Msg 1"), ELogVerbosity::Error, FName(TEXT("LogTest")));
    Device.Serialize(TEXT("Msg 2"), ELogVerbosity::Error, FName(TEXT("LogTest")));
    Device.Serialize(TEXT("Msg 3"), ELogVerbosity::Error, FName(TEXT("LogTest")));
    TestEqual(TEXT("队列应达到满额容量 3"), Device.GetQueuedCount(), 3);
    TestEqual(TEXT("未满前丢弃数应为 0"), Device.GetDroppedCount(), int64(0));

    Device.Serialize(TEXT("Msg 4 Overflow"), ELogVerbosity::Error, FName(TEXT("LogTest")));
    TestEqual(TEXT("溢出后队列容量应严格维持 3"), Device.GetQueuedCount(), 3);
    TestEqual(TEXT("溢出日志应计入丢弃计数器"), Device.GetDroppedCount(), int64(1));

    Device.Clear();
    TestTrue(TEXT("清空后队列应为空"), Device.IsEmpty());
    TestEqual(TEXT("清空后队列数量应为 0"), Device.GetQueuedCount(), 0);
    TestEqual(TEXT("清空后丢弃数量应为 0"), Device.GetDroppedCount(), int64(0));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameRemoteLogOutputDeviceConcurrencyAndReentrancyTest,
    "Game.Logging.RemoteOutputDevice.ConcurrencyAndReentrancy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证嵌套重入守卫、多线程严格容量守恒、以及并发读写黑名单与入队压测
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameRemoteLogOutputDeviceConcurrencyAndReentrancyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameRemoteLogOutputDevice Device;

    TestFalse(TEXT("默认不处于重入上下文"), Game::Log::Private::FRemoteLogReentrancyScope::IsHandling());
    {
        const Game::Log::Private::FRemoteLogReentrancyScope OuterGuard;
        TestTrue(TEXT("进入外层守卫后 IsHandling 为 true"), Game::Log::Private::FRemoteLogReentrancyScope::IsHandling());
        {
            const Game::Log::Private::FRemoteLogReentrancyScope InnerGuard;
            TestTrue(TEXT("嵌套内层守卫保持 IsHandling 为 true"), Game::Log::Private::FRemoteLogReentrancyScope::IsHandling());
            Device.Serialize(TEXT("BlockedByReentrancy"), ELogVerbosity::Error, FName(TEXT("LogReentrancy")));
        }
        TestTrue(TEXT("退出内层守卫后仍应保持外层重入状态"), Game::Log::Private::FRemoteLogReentrancyScope::IsHandling());
    }
    TestFalse(TEXT("退出外层守卫后应恢复非重入状态"), Game::Log::Private::FRemoteLogReentrancyScope::IsHandling());
    TestEqual(TEXT("重入守卫激活期间写入的日志必须被彻底阻断"), Device.GetQueuedCount(), 0);

    Device.SetMaxCapacity(16);
    constexpr int32 BurstThreads = 8;
    constexpr int32 LogsPerBurstThread = 50;
    constexpr int32 TotalBurstLogs = BurstThreads * LogsPerBurstThread;
    ParallelFor(BurstThreads, [&Device](int32 ThreadIdx) {
        for (int32 Step = 0; Step < LogsPerBurstThread; ++Step) {
            const FString Msg = FString::Printf(TEXT("Burst_T%d_%d"), ThreadIdx, Step);
            Device.Serialize(*Msg, ELogVerbosity::Error, FName(TEXT("LogBurst")));
        }
    });
    TestEqual(TEXT("高并发突发写入下排队数不得突破软上限 16"), Device.GetQueuedCount(), 16);
    TestEqual(
        TEXT("高并发下 排队数 + 丢弃数 必须精确守恒等于总产生数"),
        static_cast<int64>(Device.GetQueuedCount()) + Device.GetDroppedCount(),
        static_cast<int64>(TotalBurstLogs)
    );

    Device.Clear();
    Device.SetMaxCapacity(FGameRemoteLogOutputDevice::MAX_CAPACITY);
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
    TestTrue(TEXT("排空后 IsEmpty 为 true"), Device.IsEmpty());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameRemoteLogOutputDeviceBatchAtomicDrainTest,
    "Game.Logging.RemoteOutputDevice.BatchAtomicDrain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证大批次排出时计数器单次合并扣减与多次 Clear 幂等一致性
 * @param Parameters 自动化测试参数（未使用）
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
    Batch2.Add(FGameRemoteLogEntry(FName(TEXT("LogExisting")), static_cast<uint8>(ELogVerbosity::Warning), TEXT("PreExisting"), 1.0, 1));
    const int32 Drained2 = Device.DrainBatch(Batch2, 200);
    TestEqual(TEXT("排空剩余全部条目"), Drained2, 150);
    TestEqual(TEXT("DrainBatch 应追加到非空输出数组并保留原有元素"), Batch2.Num(), 151);
    TestEqual(TEXT("原首项应保持不变"), Batch2[0].Message, FString(TEXT("PreExisting")));
    TestEqual(TEXT("剩余队列计数应为 0"), Device.GetQueuedCount(), 0);

    Device.Clear();
    Device.Clear();
    TestEqual(TEXT("多次 Clear 不会产生负数或状态错乱"), Device.GetQueuedCount(), 0);

    return true;
}

#endif // WITH_AUTOMATION_TESTS
