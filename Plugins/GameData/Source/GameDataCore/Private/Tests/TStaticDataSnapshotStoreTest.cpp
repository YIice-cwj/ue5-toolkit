/**
 * @file TStaticDataSnapshotStoreTest.cpp
 * @brief 泛型静态数据快照 RCU 存储模板 (TStaticDataSnapshotStore) 自动化测试
 */

#include "StaticData/TStaticDataSnapshotStore.h"

#if WITH_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Core/DataErrorCodes.h"
#include "Misc/AutomationTest.h"

#include <atomic>

namespace {

/**
 * @brief 泛型快照存储器测试探针结构体
 */
struct FTestProbeSnapshot {
    uint32 DataVersion = 0;
    int32 PayloadChecksum = 0;
};

constexpr int32 ProbeGenerationCount = 64;
constexpr int32 ProbeReaderWorkers = 16;
constexpr int32 ProbeReaderMinIterations = 6250;
constexpr int32 ProbeReaderMaxIterations = 20000;

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTStaticDataSnapshotStoreTest,
    "Game.Data.SnapshotStore.GenericTemplate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证泛型 RCU 存储的无锁版本读取、原子发布、空快照清空、并发读写与门闩错误码契约
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FTStaticDataSnapshotStoreTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TStaticDataSnapshotStore<FTestProbeSnapshot> Store;

    TestFalse(TEXT("初始快照应为空"), Store.Acquire().IsValid());
    TestEqual(TEXT("初始版本号应为 0"), Store.GetVersion(), 0u);
    TestFalse(TEXT("初始重载门闩应处于空闲态"), Store.IsReloadInProgress());

    const TGameStatusResult<void> FirstLatch = Store.TryBeginReload();
    TestTrue(TEXT("首次抢占重载门闩应成功"), FirstLatch.IsSuccess());
    TestTrue(TEXT("抢占后 IsReloadInProgress 应为 true"), Store.IsReloadInProgress());

    const TGameStatusResult<void> SecondLatch = Store.TryBeginReload();
    if (TestTrue(TEXT("重入抢占门闩应失败"), SecondLatch.IsFailure())) {
        TestEqual(TEXT("重入失败模块归属应为 Game::ModuleId::Data"), SecondLatch.GetError().ModuleId, Game::ModuleId::Data);
        TestEqual(TEXT("重入失败错误码应为 ReloadInProgress"), SecondLatch.GetError().ErrorCode, Game::DataErrorCodes::ReloadInProgress);
    }

    Store.EndReload();
    TestFalse(TEXT("释放门闩后 IsReloadInProgress 应为 false"), Store.IsReloadInProgress());

    {
        TestTrue(TEXT("测试票据抢占门闩成功"), Store.TryBeginReload().IsSuccess());
        auto Ticket = Store.MakeReloadLatchTicket();
        TestTrue(TEXT("持有票据期间门闩应保持占用"), Store.IsReloadInProgress());
    }
    TestFalse(TEXT("票据析构后应自动释放重载门闩"), Store.IsReloadInProgress());

    TestTrue(TEXT("再次抢占门闩成功"), Store.TryBeginReload().IsSuccess());
    auto ExplicitTicket = Store.MakeReloadLatchTicket();
    ExplicitTicket->Release();
    TestFalse(TEXT("显式 Release 后门闩应立即释放"), Store.IsReloadInProgress());
    TestTrue(TEXT("下一轮重载抢占门闩成功"), Store.TryBeginReload().IsSuccess());
    ExplicitTicket->Release();
    TestTrue(TEXT("旧票据幂等 Release 不得误释放新一轮门闩"), Store.IsReloadInProgress());
    Store.EndReload();
    TestFalse(TEXT("结束新一轮门闩后恢复空闲"), Store.IsReloadInProgress());

    TestTrue(TEXT("ABA 场景首轮抢占门闩成功"), Store.TryBeginReload().IsSuccess());
    auto StaleTicketBeforeEndReload = Store.MakeReloadLatchTicket();
    Store.EndReload();
    TestFalse(TEXT("EndReload 直接释放门闩后处于空闲态"), Store.IsReloadInProgress());
    TestTrue(TEXT("广播重入或新一轮 TryBeginReload 抢占新代次门闩成功"), Store.TryBeginReload().IsSuccess());
    StaleTicketBeforeEndReload->Release();
    TestTrue(TEXT("EndReload 前签发的旧代次票据首次调用 Release 绝不误释放新一轮门闩"), Store.IsReloadInProgress());
    Store.EndReload();
    TestFalse(TEXT("清理 ABA 测试门闩后恢复空闲"), Store.IsReloadInProgress());

    TArray<TSharedPtr<const FTestProbeSnapshot>> Snapshots;
    Snapshots.Reserve(ProbeGenerationCount);
    for (int32 Index = 1; Index <= ProbeGenerationCount; ++Index) {
        TSharedPtr<FTestProbeSnapshot> Snap = MakeShared<FTestProbeSnapshot>();
        Snap->DataVersion = static_cast<uint32>(Index);
        Snap->PayloadChecksum = Index * 31;
        Snapshots.Add(MoveTemp(Snap));
    }

    Store.Publish(Snapshots[0]);
    TestEqual(TEXT("首次发布后无锁 GetVersion 应为 1"), Store.GetVersion(), 1u);
    const TSharedPtr<const FTestProbeSnapshot> HeldFirst = Store.Acquire();
    TestTrue(TEXT("持有首代快照应有效"), HeldFirst.IsValid());

    std::atomic<bool> bWriterDone{false};
    std::atomic<int32> ErrorCount{0};
    std::atomic<int32> TotalReads{0};

    const TFuture<void> WriterFuture = Async(EAsyncExecution::Thread, [&Store, &Snapshots, &bWriterDone, &ErrorCount]() {
        for (int32 Index = 1; Index < Snapshots.Num(); ++Index) {
            Store.Publish(Snapshots[Index]);
            const uint32 VersionAfterPublish = Store.GetVersion();
            const TSharedPtr<const FTestProbeSnapshot> Probe = Store.Acquire();
            if (VersionAfterPublish < Snapshots[Index]->DataVersion || !Probe.IsValid() || Probe->DataVersion != VersionAfterPublish) {
                ErrorCount.fetch_add(1);
            }
        }
        bWriterDone.store(true, std::memory_order_release);
    });

    ParallelFor(ProbeReaderWorkers, [&Store, &bWriterDone, &ErrorCount, &TotalReads]([[maybe_unused]] int32 WorkerIdx) {
        int32 LocalReads = 0;
        uint32 LastVer = 0;
        while (LocalReads < ProbeReaderMaxIterations
            && (!bWriterDone.load(std::memory_order_acquire) || LocalReads < ProbeReaderMinIterations)) {
            ++LocalReads;
            const uint32 FastVer = Store.GetVersion();
            const TSharedPtr<const FTestProbeSnapshot> Snap = Store.Acquire();
            if (!Snap.IsValid() || Snap->DataVersion < LastVer || Snap->DataVersion < FastVer
                || Snap->PayloadChecksum != static_cast<int32>(Snap->DataVersion) * 31) {
                ErrorCount.fetch_add(1);
                continue;
            }
            LastVer = Snap->DataVersion;
        }
        TotalReads.fetch_add(LocalReads);
    });
    WriterFuture.Wait();

    TestEqual(TEXT("并发读写期间版本与载荷完整性零异常"), ErrorCount.load(), 0);
    TestTrue(TEXT("总并发读次数应超过十万次"), TotalReads.load() >= 100000);
    TestEqual(TEXT("旧快照持有者内容零污染"), HeldFirst->DataVersion, 1u);
    TestEqual(TEXT("并发结束后最终版本号应为末代版本"), Store.GetVersion(), static_cast<uint32>(ProbeGenerationCount));

    Store.Publish(nullptr);
    TestFalse(TEXT("发布空指针后 Acquire 应为空"), Store.Acquire().IsValid());
    TestEqual(TEXT("发布空指针后 GetVersion 应归零"), Store.GetVersion(), 0u);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
