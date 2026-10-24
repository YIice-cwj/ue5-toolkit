/**
 * @file FStaticDataSnapshotStoreTest.cpp
 * @brief 静态数据快照 RCU 存储模块内自动化测试（并发发布 / 重载门闩），Shipping 构建自动剔除
 */

#include "Generated/Data/StaticData/FStaticDataSnapshotStore.h"

#if WITH_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Misc/AutomationTest.h"

#include "Generated/Data/StaticData/FStaticDataSnapshot.h"

#include <atomic>

namespace {

constexpr int32 SnapshotGenerationCount = 64;
constexpr int32 SnapshotReaderWorkers = 16;
constexpr int32 SnapshotReaderMinIterations = 6250;
constexpr int32 SnapshotReaderMaxIterations = 20000;

/**
 * @brief 构造带版本探针的测试快照（版本号即内容探针，域无关）
 * @param Version 数据版本号
 * @return 只读快照共享指针
 */
TSharedPtr<const FStaticDataSnapshot> MakeProbeSnapshot(uint32 Version) {
    TSharedPtr<FStaticDataSnapshot> Snapshot = MakeShared<FStaticDataSnapshot>();
    Snapshot->DataVersion = Version;
    return Snapshot;
}

/**
 * @brief 读取快照内版本探针
 * @param Snapshot 目标快照
 * @return 版本号，无效快照返回 0
 */
uint32 ReadProbeVersion(const TSharedPtr<const FStaticDataSnapshot>& Snapshot) {
    return Snapshot.IsValid() ? Snapshot->DataVersion : 0u;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotStoreConcurrentPublishTest, "Game.Data.Snapshot.ConcurrentPublish",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 并发发布：1 写线程反复 Publish 与 16 读线程反复 Acquire 对撞
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：预置 64 个版本各异的探针快照，读者至少各跑 6250 轮、写线程未收工时继续加压（合计超十万次）；
 * 断言全程零崩溃、单读者版本单调不减、每次读到的快照版本自洽、旧持有者不被后续发布污染、
 * 发布返回后立刻取快照必命中新一代
 */
bool FSnapshotStoreConcurrentPublishTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FStaticDataSnapshotStore Store;
    TestFalse(TEXT("尚未发布时取快照返回空"), Store.Acquire().IsValid());
    TestEqual(TEXT("尚未发布时版本号为 0"), Store.GetVersion(), 0u);

    TArray<TSharedPtr<const FStaticDataSnapshot>> Snapshots;
    Snapshots.Reserve(SnapshotGenerationCount);
    for (int32 Index = 1; Index <= SnapshotGenerationCount; ++Index) {
        Snapshots.Add(MakeProbeSnapshot(static_cast<uint32>(Index)));
    }
    Store.Publish(Snapshots[0]);
    TestEqual(TEXT("首次发布后版本号就位"), Store.GetVersion(), 1u);

    const TSharedPtr<const FStaticDataSnapshot> HeldSnapshot = Store.Acquire();
    TestTrue(TEXT("并发前持有第一代快照成功"), HeldSnapshot.IsValid());

    std::atomic<bool> bWriterFinished{false};
    std::atomic<int32> InvalidReadCount{0};
    std::atomic<int32> TotalReadCount{0};
    std::atomic<int32> PublishProbeMissCount{0};

    const TFuture<void> WriterFuture = Async(EAsyncExecution::Thread, [&Store, &Snapshots, &bWriterFinished, &PublishProbeMissCount]() {
        for (int32 Index = 1; Index < Snapshots.Num(); ++Index) {
            Store.Publish(Snapshots[Index]);
            const TSharedPtr<const FStaticDataSnapshot> Probe = Store.Acquire();
            if (!Probe.IsValid() || Probe->DataVersion < Snapshots[Index]->DataVersion) {
                PublishProbeMissCount.fetch_add(1);
            }
        }
        bWriterFinished.store(true);
    });

    ParallelFor(SnapshotReaderWorkers, [&Store, &bWriterFinished, &InvalidReadCount, &TotalReadCount]([[maybe_unused]] int32 WorkerIndex) {
        int32 LocalIterations = 0;
        int32 LocalInvalidCount = 0;
        uint32 LastVersion = 0;
        while (LocalIterations < SnapshotReaderMaxIterations
            && (!bWriterFinished.load() || LocalIterations < SnapshotReaderMinIterations)) {
            ++LocalIterations;
            const uint32 SnapshotVersion = ReadProbeVersion(Store.Acquire());
            if (SnapshotVersion == 0 || SnapshotVersion < LastVersion) {
                ++LocalInvalidCount;
                continue;
            }
            LastVersion = SnapshotVersion;
        }
        TotalReadCount.fetch_add(LocalIterations);
        if (LocalInvalidCount > 0) {
            InvalidReadCount.fetch_add(LocalInvalidCount);
        }
    });
    WriterFuture.Wait();

    TestTrue(TEXT("写线程已完成全部版本发布"), bWriterFinished.load());
    TestEqual(TEXT("发布返回后立刻取快照必命中新一代"), PublishProbeMissCount.load(), 0);
    TestEqual(TEXT("并发发布期间读取结果始终合法（零崩溃/版本单调不减）"), InvalidReadCount.load(), 0);
    TestTrue(TEXT("读取操作总数超过十万次"), TotalReadCount.load() >= 100000);
    TestTrue(TEXT("并发发布期间旧持有者仍存活"), HeldSnapshot.IsValid());
    TestEqual(TEXT("并发发布期间旧持有者版本未被污染"), ReadProbeVersion(HeldSnapshot), 1u);
    TestEqual(TEXT("发布结束后版本号为末次发布版本"), Store.GetVersion(), static_cast<uint32>(SnapshotGenerationCount));
    TestEqual(TEXT("发布结束后必命中末代快照"), ReadProbeVersion(Store.Acquire()), static_cast<uint32>(SnapshotGenerationCount));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotStoreReloadLatchTest, "Game.Data.Snapshot.ReloadLatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 重载门闩：同一时刻仅允许一个写者进入提交流程
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：抢占成功 → 二次抢占必失败 → 释放后恢复可抢占，断言门闩互斥与释放语义
 */
bool FSnapshotStoreReloadLatchTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FStaticDataSnapshotStore Store;

    TestTrue(TEXT("首次抢占重载门闩成功"), Store.TryBeginReload());
    TestFalse(TEXT("重载进行中二次抢占必失败"), Store.TryBeginReload());

    Store.EndReload();
    TestTrue(TEXT("释放门闩后可再次抢占"), Store.TryBeginReload());
    TestFalse(TEXT("再次抢占后二次抢占仍失败"), Store.TryBeginReload());
    Store.EndReload();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
