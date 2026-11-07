/**
 * @file FGameAssetLoaderTest.cpp
 * @brief FGameAssetLoadHandle 与 FGameAssetLoader 弱引用异步软资产加载单元测试
 */

#include "Asset/FGameAssetLoader.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"

namespace {

struct FAsyncAssetTestState {
    TStrongObjectPtr<UGameInstance> AliveOwner;
    FGameAssetLoadHandle ActiveHandle;
    UStaticMesh* LoadedMesh = nullptr;
    bool bCallbackInvoked = false;
    int32 FramesWaited = 0;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FWaitForAssetLoadLatentCommand,
    FAutomationTestBase&,
    TestRunner,
    TSharedRef<FAsyncAssetTestState>,
    SharedState
);

bool FWaitForAssetLoadLatentCommand::Update() {
    if (SharedState->bCallbackInvoked) {
        TestRunner.TestNotNull(TEXT("异步磁盘软引用成功解析出 UStaticMesh 实例"), SharedState->LoadedMesh);
        TestRunner.TestTrue(TEXT("加载完成后句柄 HasCompleted 为真"), SharedState->ActiveHandle.HasCompleted());
        TestRunner.TestEqual(TEXT("加载完成后进度为 1.0"), SharedState->ActiveHandle.GetProgress(), 1.0f);
        SharedState->AliveOwner.Reset();
        return true;
    }

    ++SharedState->FramesWaited;
    if (SharedState->FramesWaited > 300) {
        TestRunner.AddError(TEXT("等待异步资产加载回调超时"));
        SharedState->AliveOwner.Reset();
        return true;
    }
    return false;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetLoaderShortCircuitAndGuardsTest,
    "Game.Core.Asset.ShortCircuitAndGuards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证空句柄状态、非法参数守卫、已驻留内存资产同步短路、主动取消以及失效宿主自动拦截
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetLoaderShortCircuitAndGuardsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameAssetLoadHandle EmptyHandle;
    TestFalse(TEXT("默认句柄无效"), EmptyHandle.IsValid());
    TestFalse(TEXT("默认句柄不在加载中"), EmptyHandle.IsLoading());
    TestFalse(TEXT("默认句柄未完成"), EmptyHandle.HasCompleted());
    TestEqual(TEXT("默认句柄进度为 0"), EmptyHandle.GetProgress(), 0.0f);
    EmptyHandle.Cancel();

    UGameInstance* Owner = NewObject<UGameInstance>();
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();

    bool bNullPathCalled = false;
    const TSoftObjectPtr<UGameInstance> NullSoftPtr;
    const FGameAssetLoadHandle NullResult = FGameAssetLoader::LoadAsync<UGameInstance>(
        NullSoftPtr,
        Owner,
        [&bNullPathCalled](UGameInstance*) { bNullPathCalled = true; }
    );
    TestFalse(TEXT("空软引用返回无效句柄"), NullResult.IsValid());
    TestFalse(TEXT("空软引用不触发回调"), bNullPathCalled);

    bool bDeadOwnerCalled = false;
    const TSoftObjectPtr<UGameInstance> ResidentSoftPtr(ResidentTarget);
    const FGameAssetLoadHandle DeadOwnerResult = FGameAssetLoader::LoadAsync<UGameInstance>(
        ResidentSoftPtr,
        nullptr,
        [&bDeadOwnerCalled](UGameInstance*) { bDeadOwnerCalled = true; }
    );
    TestFalse(TEXT("空宿主返回无效句柄"), DeadOwnerResult.IsValid());
    TestFalse(TEXT("空宿主绝不触发回调"), bDeadOwnerCalled);

    UGameInstance* ResolvedResident = nullptr;
    const FGameAssetLoadHandle ResidentHandle = FGameAssetLoader::LoadAsync<UGameInstance>(
        ResidentSoftPtr,
        Owner,
        [&ResolvedResident](UGameInstance* Loaded) { ResolvedResident = Loaded; }
    );
    TestFalse(TEXT("已驻留内存对象走同步短路无需保留流式句柄"), ResidentHandle.IsValid());
    TestEqual(TEXT("已驻留内存对象同步精确传回目标实例"), ResolvedResident, ResidentTarget);

    bool bEmptyBatchDone = false;
    const TArray<FSoftObjectPath> EmptyPaths = {FSoftObjectPath()};
    FGameAssetLoader::LoadBatchAsync(EmptyPaths, Owner, [&bEmptyBatchDone]() { bEmptyBatchDone = true; });
    TestTrue(TEXT("过滤无效路径后为空批次时立即通知完成"), bEmptyBatchDone);

    bool bResidentBatchDone = false;
    const TArray<FSoftObjectPath> ResidentPaths = {FSoftObjectPath(ResidentTarget)};
    FGameAssetLoader::LoadBatchAsync(ResidentPaths, Owner, [&bResidentBatchDone]() { bResidentBatchDone = true; });
    TestTrue(TEXT("全部路径均已驻留内存时批量接口同步完成"), bResidentBatchDone);

    bool bCancelledBatchInvoked = false;
    const TArray<FSoftObjectPath> UnloadedPaths = {
        FSoftObjectPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"))
    };
    FGameAssetLoadHandle CancelBatchHandle = FGameAssetLoader::LoadBatchAsync(
        UnloadedPaths,
        Owner,
        [&bCancelledBatchInvoked]() { bCancelledBatchInvoked = true; }
    );
    if (CancelBatchHandle.IsValid()) {
        CancelBatchHandle.Cancel();
        TestFalse(TEXT("取消后句柄立即变为无效"), CancelBatchHandle.IsValid());
        TestEqual(TEXT("取消后进度归零"), CancelBatchHandle.GetProgress(), 0.0f);
        FlushAsyncLoading();
        TestFalse(TEXT("已取消请求在 FlushAsyncLoading 后绝不触发完成回调"), bCancelledBatchInvoked);
    }

    UGameInstance* TransientDyingOwner = NewObject<UGameInstance>();
    bool bDyingAsyncCallbackFired = false;
    const TSoftObjectPtr<UStaticMesh> SphereSoft(FSoftObjectPath(TEXT("/Engine/BasicShapes/Torus.Torus")));
    [[maybe_unused]] FGameAssetLoadHandle DyingAsyncHandle = FGameAssetLoader::LoadAsync<UStaticMesh>(
        SphereSoft,
        TransientDyingOwner,
        [&bDyingAsyncCallbackFired](UStaticMesh*) { bDyingAsyncCallbackFired = true; }
    );
    TransientDyingOwner->MarkAsGarbage();
    FlushAsyncLoading();
    TestFalse(TEXT("异步回调执行前宿主销毁时自动短路绝不解引用"), bDyingAsyncCallbackFired);

    Owner->MarkAsGarbage();
    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetLoaderDiskStreamingLatentTest,
    "Game.Core.Asset.DiskStreamingLatent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 通过引擎 Latent 帧循环真实验证未驻留引擎资产的异步加载与完成投递（零 Sleep 盲等）
 * @param Parameters 自动化测试参数
 * @return Latent 命令挂载成功返回 true
 */
bool FGameAssetLoaderDiskStreamingLatentTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSharedRef<FAsyncAssetTestState> State = MakeShared<FAsyncAssetTestState>();
    State->AliveOwner = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>());

    const TSoftObjectPtr<UStaticMesh> ConeSoft(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cone.Cone")));
    State->ActiveHandle = FGameAssetLoader::LoadAsync<UStaticMesh>(
        ConeSoft,
        State->AliveOwner.Get(),
        [State](UStaticMesh* Loaded) {
            State->LoadedMesh = Loaded;
            State->bCallbackInvoked = true;
        }
    );

    if (State->bCallbackInvoked) {
        TestNotNull(TEXT("资产若已缓存于内存亦正确返回 UStaticMesh"), State->LoadedMesh);
        State->AliveOwner.Reset();
        return true;
    }

    TestTrue(TEXT("未驻留资产返回有效异步加载句柄"), State->ActiveHandle.IsValid());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForAssetLoadLatentCommand(*this, State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetLoaderClassLoadingTest,
    "Game.Core.Asset.ClassLoading",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证软引用类的异步安全加载、短路判定与同步获取
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetLoaderClassLoadingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* Owner = NewObject<UGameInstance>();

    bool bNullClassCalled = false;
    const TSoftClassPtr<UGameInstance> NullSoftClass;
    const FGameAssetLoadHandle NullHandle = FGameAssetLoader::LoadClassAsync<UGameInstance>(
        NullSoftClass,
        Owner,
        [&bNullClassCalled](UClass*) { bNullClassCalled = true; }
    );
    TestFalse(TEXT("空软类引用返回无效句柄"), NullHandle.IsValid());
    TestFalse(TEXT("空软类引用不触发回调"), bNullClassCalled);

    const TSoftClassPtr<UGameInstance> ResidentClassPtr(UGameInstance::StaticClass());
    bool bResidentClassCalled = false;
    UClass* LoadedResidentClass = nullptr;
    const FGameAssetLoadHandle ResidentHandle = FGameAssetLoader::LoadClassAsync<UGameInstance>(
        ResidentClassPtr,
        Owner,
        [&bResidentClassCalled, &LoadedResidentClass](UClass* ClassPtr) {
            bResidentClassCalled = true;
            LoadedResidentClass = ClassPtr;
        }
    );
    TestFalse(TEXT("已驻留内存的类走同步短路返回空句柄"), ResidentHandle.IsValid());
    TestTrue(TEXT("已驻留内存的类同步短路触发回调"), bResidentClassCalled);
    TestEqual(TEXT("短路回调精确返回 UGameInstance::StaticClass()"), LoadedResidentClass, UGameInstance::StaticClass());

    UClass* NullSyncResult = FGameAssetLoader::LoadClassSync<UGameInstance>(NullSoftClass);
    TestNull(TEXT("同步加载空软类引用返回 nullptr"), NullSyncResult);

    UClass* ResidentSyncResult = FGameAssetLoader::LoadClassSync<UGameInstance>(ResidentClassPtr);
    TestEqual(TEXT("同步加载已驻留类返回正确 UClass 指针"), ResidentSyncResult, UGameInstance::StaticClass());

    Owner->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetLoaderSyncAndPinningTest,
    "Game.Core.Asset.SyncAndPinning",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证资产同步加载、内存保活引用计数与 RAII 作用域守卫生命周期
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetLoaderSyncAndPinningTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameAssetLoader::ClearAllPinnedAssets();

    const FSoftObjectPath EmptyPath;
    TestFalse(TEXT("锁定空路径返回 false"), FGameAssetLoader::PinAsset(EmptyPath));
    TestFalse(TEXT("空路径 IsAssetPinned 为假"), FGameAssetLoader::IsAssetPinned(EmptyPath));
    TestFalse(TEXT("解除空路径返回 false"), FGameAssetLoader::UnpinAsset(EmptyPath));

    UGameInstance* ResidentTarget = NewObject<UGameInstance>();
    const FSoftObjectPath ResidentPath(ResidentTarget);

    TestFalse(TEXT("尚未锁定的路径 IsAssetPinned 为假"), FGameAssetLoader::IsAssetPinned(ResidentPath));
    TestFalse(TEXT("解除未锁定的路径返回 false"), FGameAssetLoader::UnpinAsset(ResidentPath));

    TestTrue(TEXT("初次锁定有效驻留路径成功"), FGameAssetLoader::PinAsset(ResidentPath));
    TestTrue(TEXT("锁定后 IsAssetPinned 为真"), FGameAssetLoader::IsAssetPinned(ResidentPath));

    TestTrue(TEXT("重复锁定同一路径递增引用计数成功"), FGameAssetLoader::PinAsset(ResidentPath));
    TestTrue(TEXT("扣减第一次引用后依然处于锁定状态"), FGameAssetLoader::UnpinAsset(ResidentPath));
    TestTrue(TEXT("一次 Unpin 后仍然被锁定"), FGameAssetLoader::IsAssetPinned(ResidentPath));

    TestTrue(TEXT("扣减第二次引用后完全解除锁定"), FGameAssetLoader::UnpinAsset(ResidentPath));
    TestFalse(TEXT("引用归零后 IsAssetPinned 为假"), FGameAssetLoader::IsAssetPinned(ResidentPath));

    {
        FScopedPinnedAsset ScopedPin(ResidentPath);
        TestTrue(TEXT("FScopedPinnedAsset 构造时接管锁定"), ScopedPin.IsPinned());
        TestTrue(TEXT("RAII 守卫内路径处于锁定状态"), FGameAssetLoader::IsAssetPinned(ResidentPath));

        FScopedPinnedAsset MovedPin(MoveTemp(ScopedPin));
        TestFalse(TEXT("移动构造后源守卫处于非锁定状态"), ScopedPin.IsPinned());
        TestTrue(TEXT("移动构造后目标守卫持有锁定"), MovedPin.IsPinned());
        TestTrue(TEXT("移动后资产仍然处于保活状态"), FGameAssetLoader::IsAssetPinned(ResidentPath));
    }
    TestFalse(TEXT("离开作用域后 RAII 守卫自动解除锁定"), FGameAssetLoader::IsAssetPinned(ResidentPath));

    UGameInstance* NullSync = FGameAssetLoader::LoadSync<UGameInstance>(TSoftObjectPtr<UGameInstance>());
    TestNull(TEXT("同步加载空软引用返回 nullptr"), NullSync);

    UGameInstance* ResidentSync = FGameAssetLoader::LoadSync<UGameInstance>(TSoftObjectPtr<UGameInstance>(ResidentTarget));
    TestEqual(TEXT("同步加载已驻留实例返回自身"), ResidentSync, ResidentTarget);

    FGameAssetLoader::ClearAllPinnedAssets();
    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetLoaderBatchWithTokenTest,
    "Game.Core.Asset.BatchWithToken",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证批量资产加载门面在空路径短路、预取消令牌拦截以及进度汇报机制
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetLoaderBatchWithTokenTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* DummyOwner = NewObject<UGameInstance>();

    FCancellationTokenSource CancelledSource;
    CancelledSource.Cancel();
    bool bCancelledCallbackInvoked = false;
    FGameAssetLoadHandle AbortedHandle = FGameAssetLoader::LoadBatchAsync(
        {FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))},
        DummyOwner,
        [&bCancelledCallbackInvoked]() {
            bCancelledCallbackInvoked = true;
        },
        nullptr,
        CancelledSource.GetToken()
    );
    TestFalse(TEXT("已取消令牌拦截加载，句柄无效"), AbortedHandle.IsValid());
    TestFalse(TEXT("已取消令牌不会触发完成回调"), bCancelledCallbackInvoked);

    bool bEmptyAllLoaded = false;
    float LastProgress = 0.0f;
    FGameAssetLoadHandle EmptyHandle = FGameAssetLoader::LoadBatchAsync(
        {},
        DummyOwner,
        [&bEmptyAllLoaded]() {
            bEmptyAllLoaded = true;
        },
        [&LastProgress](float Progress) {
            LastProgress = Progress;
        },
        FCancellationToken::None()
    );
    TestFalse(TEXT("空路径短路返回空句柄"), EmptyHandle.IsValid());
    TestTrue(TEXT("空路径立即触发 OnAllLoaded"), bEmptyAllLoaded);
    TestEqual(TEXT("空路径汇报进度为 1.0"), LastProgress, 1.0f);

    DummyOwner->MarkAsGarbage();
    return true;
}

#endif
