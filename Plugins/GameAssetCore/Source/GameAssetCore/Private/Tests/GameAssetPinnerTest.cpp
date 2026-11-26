/**
 * @file GameAssetPinnerTest.cpp
 * @brief FGameAssetPinner 与 FScopedPinnedAsset 引用计数、真 GC 存活验证、跨线程句柄释放与 RAII 守卫测试
 */

#include "Pinning/FGameAssetPinner.h"
#include "Pinning/FScopedPinnedAsset.h"

#include "Async/Async.h"
#include "Async/FGameThreadTaskRunner.h"
#include "Async/ParallelFor.h"
#include "Core/AssetErrorCodes.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetPinnerLifecycleAndErrorsTest,
    "Game.Asset.Pinner.LifecycleAndErrorCodes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameAssetPinner 引用计数增减、错误码（3001/3002/3004/3005）、磁盘加载跨线程安全解绑、失效条目统计一致性与清空机制
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetPinnerLifecycleAndErrorsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameAssetPinner::ClearAllPinnedAssets();
    FGameAssetPinner::Startup();

    const FSoftObjectPath EmptyPath;
    const TGameStatusResult<UObject*> EmptyPinRes = FGameAssetPinner::PinAsset(EmptyPath);
    TestTrue(TEXT("锁定空路径返回失败"), EmptyPinRes.IsFailure());
    TestEqual(TEXT("空路径错误码为 InvalidAssetPath (3001)"), EmptyPinRes.GetError().ErrorCode, Game::AssetErrorCodes::InvalidAssetPath);
    TestFalse(TEXT("空路径 IsAssetPinned 为假"), FGameAssetPinner::IsAssetPinned(EmptyPath));
    TestEqual(TEXT("空路径引用计数为 0"), FGameAssetPinner::GetPinRefCount(EmptyPath), 0);
    TestNull(TEXT("空路径 GetPinnedAsset 返回 nullptr"), FGameAssetPinner::GetPinnedAsset(EmptyPath));

    const TGameStatusResult<void> EmptyUnpinRes = FGameAssetPinner::UnpinAsset(EmptyPath);
    TestTrue(TEXT("解除空路径返回失败"), EmptyUnpinRes.IsFailure());
    TestEqual(TEXT("解除空路径错误码为 3001"), EmptyUnpinRes.GetError().ErrorCode, Game::AssetErrorCodes::InvalidAssetPath);

    UGameInstance* ResidentTarget = NewObject<UGameInstance>();
    const FSoftObjectPath ResidentPath(ResidentTarget);

    const TGameStatusResult<void> UnpinnedRes = FGameAssetPinner::UnpinAsset(ResidentPath);
    TestTrue(TEXT("解除尚未锁定的路径返回失败"), UnpinnedRes.IsFailure());
    TestEqual(TEXT("未锁定路径错误码为 AssetNotPinned (3004)"), UnpinnedRes.GetError().ErrorCode, Game::AssetErrorCodes::AssetNotPinned);

    const TGameStatusResult<UObject*> MissingPinRes = FGameAssetPinner::PinAsset(
        FSoftObjectPath(TEXT("/Game/__UnitTest_MissingPinAsset.__UnitTest_MissingPinAsset"))
    );
    TestTrue(TEXT("锁定不存在的磁盘路径返回失败"), MissingPinRes.IsFailure());
    TestEqual(TEXT("不存在路径锁定返回 LoadFailed (3002)"), MissingPinRes.GetError().ErrorCode, Game::AssetErrorCodes::LoadFailed);

    TFuture<int32> WorkerPinErrorFuture = Async(EAsyncExecution::ThreadPool, []() {
        const TGameStatusResult<UObject*> BgPin = FGameAssetPinner::PinAsset(
            FSoftObjectPath(TEXT("/Game/__UnitTest_WorkerPinUnloaded.__UnitTest_WorkerPinUnloaded"))
        );
        return BgPin.IsFailure() ? BgPin.GetError().ErrorCode : 0;
    });
    TestEqual(TEXT("后台线程对未驻留资产请求 PinAsset 返回 InvalidThread (3005)"), WorkerPinErrorFuture.Get(), Game::AssetErrorCodes::InvalidThread);

    const TGameStatusResult<UObject*> Pin1 = FGameAssetPinner::PinAsset(ResidentPath);
    TestTrue(TEXT("首次锁定驻留路径成功"), Pin1.IsSuccess());
    if (Pin1.IsSuccess()) {
        TestEqual(TEXT("PinAsset 直接返回被锁定实例指针"), Pin1.GetValue(), static_cast<UObject*>(ResidentTarget));
    }
    TestTrue(TEXT("锁定后 IsAssetPinned 为真"), FGameAssetPinner::IsAssetPinned(ResidentPath));
    TestEqual(TEXT("首次锁定后引用计数为 1"), FGameAssetPinner::GetPinRefCount(ResidentPath), 1);
    TestEqual(TEXT("GetPinnedAsset<UGameInstance> 匹配"), FGameAssetPinner::GetPinnedAsset<UGameInstance>(ResidentPath), ResidentTarget);
    TestNull(TEXT("GetPinnedAsset<UStaticMesh> 类型不兼容返回 nullptr"), FGameAssetPinner::GetPinnedAsset<UStaticMesh>(ResidentPath));

    const FSoftObjectPath DiskMeshPath(TEXT("/Engine/BasicShapes/Cube1.Cube1"));
    const TGameStatusResult<UObject*> DiskPin = FGameAssetPinner::PinAsset(DiskMeshPath);
    TestTrue(TEXT("锁定独立冷磁盘引擎资产 Cube1 成功"), DiskPin.IsSuccess());
    if (DiskPin.IsSuccess()) {
        TestNotNull(TEXT("锁定引擎磁盘资产返回非空 UStaticMesh"), Cast<UStaticMesh>(DiskPin.GetValue()));
    }
    TestEqual(TEXT("全局常驻种类总数为 2"), FGameAssetPinner::GetTotalPinnedCount(), 2);

    TArray<FSoftObjectPath> ExportedPaths;
    FGameAssetPinner::GetAllPinnedPaths(ExportedPaths);
    TestEqual(TEXT("导出的常驻路径数为 2"), ExportedPaths.Num(), 2);

    TFuture<bool> WorkerUnpinDiskFuture = Async(EAsyncExecution::ThreadPool, [DiskMeshPath]() {
        return FGameAssetPinner::UnpinAsset(DiskMeshPath).IsSuccess();
    });
    TestTrue(TEXT("工作线程解除磁盘常驻资产归零时安全将流式句柄投递回主线程释放"), WorkerUnpinDiskFuture.Get());
    FGameThreadTaskRunner::FlushInFlightTasks();
    TestFalse(TEXT("工作线程归零解除后磁盘资产不再处于常驻态"), FGameAssetPinner::IsAssetPinned(DiskMeshPath));
    TestEqual(TEXT("解除磁盘资产后全局常驻总数为 1"), FGameAssetPinner::GetTotalPinnedCount(), 1);

    const TGameStatusResult<UObject*> Pin2 = FGameAssetPinner::PinAsset(ResidentPath);
    TestTrue(TEXT("重复锁定递增引用计数成功"), Pin2.IsSuccess());
    TestEqual(TEXT("重复锁定后引用计数为 2"), FGameAssetPinner::GetPinRefCount(ResidentPath), 2);

    TestTrue(TEXT("第一次 Unpin 成功"), FGameAssetPinner::UnpinAsset(ResidentPath).IsSuccess());
    TestEqual(TEXT("第一次 Unpin 后引用计数为 1"), FGameAssetPinner::GetPinRefCount(ResidentPath), 1);
    TestTrue(TEXT("第二次 Unpin 归零成功"), FGameAssetPinner::UnpinAsset(ResidentPath).IsSuccess());
    TestFalse(TEXT("归零后 IsAssetPinned 为假"), FGameAssetPinner::IsAssetPinned(ResidentPath));
    TestNull(TEXT("归零后 GetPinnedAsset 返回 nullptr"), FGameAssetPinner::GetPinnedAsset(ResidentPath));

    UGameInstance* ExternallyGarbageTarget = NewObject<UGameInstance>();
    const FSoftObjectPath GarbagePath(ExternallyGarbageTarget);
    TestTrue(TEXT("锁定临时对象用于失效条目统计一致性校验"), FGameAssetPinner::PinAsset(GarbagePath).IsSuccess());
    ExternallyGarbageTarget->MarkAsGarbage();
    TArray<FSoftObjectPath> ValidPathsAfterGarbage;
    FGameAssetPinner::GetAllPinnedPaths(ValidPathsAfterGarbage);
    TestFalse(TEXT("外部标记垃圾对象不再视为有效常驻"), FGameAssetPinner::IsAssetPinned(GarbagePath));
    TestEqual(TEXT("GetTotalPinnedCount 与 GetAllPinnedPaths 口径严格一致（0 项）"), FGameAssetPinner::GetTotalPinnedCount(), ValidPathsAfterGarbage.Num());

    FGameAssetPinner::ClearAllPinnedAssets();
    TestEqual(TEXT("ClearAllPinnedAssets 后常驻总数为 0"), FGameAssetPinner::GetTotalPinnedCount(), 0);

    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetPinnerGarbageCollectionSurvivalTest,
    "Game.Asset.Pinner.GarbageCollectionSurvival",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 真实验证无任何外部 UPROPERTY 强引用的临时对象在 PinAsset 期间跨越 CollectGarbage 100% 存活，且在 Unpin 归零后正常被 GC 回收
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetPinnerGarbageCollectionSurvivalTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameAssetPinner::ClearAllPinnedAssets();
    FGameAssetPinner::Startup();

    TWeakObjectPtr<UGameInstance> UnrootedWeak;
    FSoftObjectPath PinnedTransientPath;
    {
        UGameInstance* UnrootedObject = NewObject<UGameInstance>();
        UnrootedWeak = UnrootedObject;
        PinnedTransientPath = FSoftObjectPath(UnrootedObject);

        const TGameStatusResult<UObject*> PinRes = FGameAssetPinner::PinAsset(PinnedTransientPath);
        TestTrue(TEXT("无根临时对象加入常驻保活成功"), PinRes.IsSuccess());
    }

    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestTrue(TEXT("执行完整 CollectGarbage 后，受 FGameAssetPinner 保活的无根对象依然存活"), UnrootedWeak.IsValid());
    TestEqual(TEXT("GC 后 GetPinnedAsset 仍准确返回存活实例"), FGameAssetPinner::GetPinnedAsset(PinnedTransientPath), static_cast<UObject*>(UnrootedWeak.Get()));

    TestTrue(TEXT("解除常驻保活成功"), FGameAssetPinner::UnpinAsset(PinnedTransientPath).IsSuccess());
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestFalse(TEXT("解除常驻保活并再次执行 CollectGarbage 后，无根对象正常被回收无泄漏"), UnrootedWeak.IsValid());

    FGameAssetPinner::ClearAllPinnedAssets();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetPinnerConcurrencyAndScopedTest,
    "Game.Asset.Pinner.ConcurrencyAndScopedRaii",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多线程并发高频 Pin/Unpin 引用计数原子一致性，以及 FScopedPinnedAsset 全构造器与移动语义
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetPinnerConcurrencyAndScopedTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameAssetPinner::ClearAllPinnedAssets();
    FGameAssetPinner::Startup();

    UGameInstance* SharedResident = NewObject<UGameInstance>();
    const FSoftObjectPath SharedPath(SharedResident);
    TestTrue(TEXT("主线程预置基础锁定"), FGameAssetPinner::PinAsset(SharedPath).IsSuccess());

    constexpr int32 WorkerCount = 8;
    constexpr int32 LoopsPerWorker = 500;
    ParallelFor(WorkerCount, [&SharedPath, SharedResident](int32) {
        for (int32 Iteration = 0; Iteration < LoopsPerWorker; ++Iteration) {
            const TGameStatusResult<UObject*> Res = FGameAssetPinner::PinAsset(SharedPath);
            check(Res.IsSuccess() && Res.GetValue() == SharedResident);
            check(FGameAssetPinner::IsAssetPinned(SharedPath));
            check(FGameAssetPinner::GetPinRefCount(SharedPath) >= 2);
            const TGameStatusResult<void> UnpinRes = FGameAssetPinner::UnpinAsset(SharedPath);
            check(UnpinRes.IsSuccess());
        }
    });

    TestEqual(TEXT("8 线程并发 4000 次增减后引用计数严格收敛回 1"), FGameAssetPinner::GetPinRefCount(SharedPath), 1);
    TestTrue(TEXT("解除基础锁定成功"), FGameAssetPinner::UnpinAsset(SharedPath).IsSuccess());

    {
        FScopedPinnedAsset EmptyGuard;
        TestFalse(TEXT("默认构造守卫未锁定"), EmptyGuard.IsPinned());
        TestFalse(TEXT("默认构造守卫布尔转换为假"), static_cast<bool>(EmptyGuard));
        TestNull(TEXT("默认构造守卫 Get 返回 nullptr"), EmptyGuard.Get());

        FScopedPinnedAsset FailedGuard(FSoftObjectPath(TEXT("/Game/__UnitTest_ScopedFailed.__UnitTest_ScopedFailed")));
        TestFalse(TEXT("加载失败路径构造的守卫 IsPinned 为假"), FailedGuard.IsPinned());
        TestTrue(TEXT("加载失败路径构造的守卫自动清空 PinnedPath"), FailedGuard.GetPath().IsNull());
        TestNull(TEXT("加载失败路径构造的守卫 Get 返回 nullptr"), FailedGuard.Get());

        const TSoftClassPtr<UGameInstance> SoftClassPtr(UGameInstance::StaticClass());
        FScopedPinnedAsset ClassScopedPin(SoftClassPtr);
        TestTrue(TEXT("由 TSoftClassPtr 构造的 RAII 守卫成功锁定"), ClassScopedPin.IsPinned());
        TestEqual(TEXT("由 TSoftClassPtr 构造的守卫解析出 UGameInstance::StaticClass()"), ClassScopedPin.Get<UClass>(), UGameInstance::StaticClass());

        FScopedPinnedAsset ObjectScopedPin{TSoftObjectPtr<UGameInstance>(SharedResident)};
        TestTrue(TEXT("由 TSoftObjectPtr 构造的守卫成功锁定"), ObjectScopedPin.IsPinned());
        TestEqual(TEXT("Get<UGameInstance> 零锁缓存命中实例一致"), ObjectScopedPin.Get<UGameInstance>(), SharedResident);

        FScopedPinnedAsset MovedPin(MoveTemp(ObjectScopedPin));
        TestFalse(TEXT("移动构造后源守卫释放所有权"), ObjectScopedPin.IsPinned());
        TestNull(TEXT("移动构造后源守卫缓存清空"), ObjectScopedPin.Get());
        TestTrue(TEXT("移动构造后新守卫持有锁定"), MovedPin.IsPinned());
        TestEqual(TEXT("移动构造过程不额外增加底层引用计数（保持 1）"), FGameAssetPinner::GetPinRefCount(SharedPath), 1);

        FScopedPinnedAsset& SelfRef = MovedPin;
        MovedPin = MoveTemp(SelfRef);
        TestTrue(TEXT("自移动赋值安全无损，守卫仍持有锁定"), MovedPin.IsPinned());

        FScopedPinnedAsset AssignedPin;
        AssignedPin = MoveTemp(MovedPin);
        TestFalse(TEXT("移动赋值后源守卫变为未锁定"), MovedPin.IsPinned());
        TestTrue(TEXT("移动赋值后目标守卫持有锁定"), AssignedPin.IsPinned());

        AssignedPin.Release();
        TestFalse(TEXT("主动 Release 后守卫立即失活"), AssignedPin.IsPinned());
        TestFalse(TEXT("主动 Release 后底层引用计数归零解锁"), FGameAssetPinner::IsAssetPinned(SharedPath));
    }

    FGameAssetPinner::ClearAllPinnedAssets();
    SharedResident->MarkAsGarbage();
    return true;
}

#endif
