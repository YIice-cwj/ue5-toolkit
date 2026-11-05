// Copyright Epic Games, Inc. All Rights Reserved.

#include "Async/Async.h"
#include "Misc/AutomationTest.h"
#include "Remote/GameRemoteLogOutputDevice.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameRemoteLogOutputDeviceTest,
    "Game.Logging.RemoteOutputDevice",
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

#endif // WITH_AUTOMATION_TESTS
