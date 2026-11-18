/**
 * @file GameAssetLoaderTest.cpp
 * @brief FGameAssetLoadHandle 与 FGameAssetLoader 软引用资产异步与协程加载测试
 */

#include "Asset/FGameAssetLoader.h"
#include "Async/FCancellationTokenSource.h"

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
        TestRunner.TestNotNull(TEXT("加载完成后句柄直接获取 GetLoadedAsset 有效"), SharedState->ActiveHandle.GetLoadedAsset());
        TestRunner.TestNotNull(TEXT("加载完成后句柄强类型 GetLoadedAsset<UStaticMesh> 有效"), SharedState->ActiveHandle.GetLoadedAsset<UStaticMesh>());
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
    "Game.Asset.AssetLoader.ShortCircuitAndGuards",
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
    TestFalse(TEXT("默认句柄无错误"), EmptyHandle.HasError());
    TestFalse(TEXT("默认句柄未取消"), EmptyHandle.WasCanceled());
    TestEqual(TEXT("默认句柄进度为 0"), EmptyHandle.GetProgress(), 0.0f);
    TestNull(TEXT("默认空句柄 GetLoadedAsset 返回 nullptr"), EmptyHandle.GetLoadedAsset());
    TArray<UObject*> EmptyAssetList;
    EmptyHandle.GetLoadedAssets(EmptyAssetList);
    TestEqual(TEXT("默认空句柄 GetLoadedAssets 为空数组"), EmptyAssetList.Num(), 0);
    EmptyHandle.Reset();
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
        TestTrue(TEXT("取消后 WasCanceled 为真"), CancelBatchHandle.WasCanceled());
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
    "Game.Asset.AssetLoader.DiskStreamingLatent",
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
    "Game.Asset.AssetLoader.ClassLoading",
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

    const TGameStatusResult<UClass*> NullSyncResult = FGameAssetLoader::LoadClassSync<UGameInstance>(NullSoftClass);
    TestTrue(TEXT("同步加载空软类引用返回失败态"), NullSyncResult.IsFailure());
    TestEqual(TEXT("空软类引用错误码为 1001"), NullSyncResult.GetError().ErrorCode, 1001);

    const TGameStatusResult<UClass*> ResidentSyncResult = FGameAssetLoader::LoadClassSync<UGameInstance>(ResidentClassPtr);
    TestTrue(TEXT("同步加载已驻留类返回成功态"), ResidentSyncResult.IsSuccess());
    TestEqual(TEXT("同步加载已驻留类返回正确 UClass 指针"), ResidentSyncResult.GetValue(), UGameInstance::StaticClass());

    Owner->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetLoaderSyncTest,
    "Game.Asset.AssetLoader.SyncLoad",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证资产同步阻塞加载成功与失败契约
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetLoaderSyncTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();

    const TGameStatusResult<UGameInstance*> NullSync = FGameAssetLoader::LoadSync<UGameInstance>(TSoftObjectPtr<UGameInstance>());
    TestTrue(TEXT("同步加载空软引用返回失败态"), NullSync.IsFailure());
    TestEqual(TEXT("同步加载空软引用错误码为 1001"), NullSync.GetError().ErrorCode, 1001);

    const TGameStatusResult<UGameInstance*> ResidentSync = FGameAssetLoader::LoadSync<UGameInstance>(TSoftObjectPtr<UGameInstance>(ResidentTarget));
    TestTrue(TEXT("同步加载已驻留实例返回成功态"), ResidentSync.IsSuccess());
    TestEqual(TEXT("同步加载已驻留实例返回自身"), ResidentSync.GetValue(), ResidentTarget);

    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetLoaderBatchWithTokenTest,
    "Game.Asset.AssetLoader.BatchWithToken",
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

    UGameInstance* ResidentTarget = NewObject<UGameInstance>();
    bool bTypedSoftBatchCalled = false;
    float TypedSoftBatchProgress = 0.0f;
    const TArray<TSoftObjectPtr<UGameInstance>> TypedSoftList = {
        TSoftObjectPtr<UGameInstance>(ResidentTarget)
    };
    FGameAssetLoadHandle TypedBatchHandle = FGameAssetLoader::LoadBatchAsync<UGameInstance>(
        TypedSoftList,
        DummyOwner,
        [&bTypedSoftBatchCalled]() {
            bTypedSoftBatchCalled = true;
        },
        [&TypedSoftBatchProgress](float Progress) {
            TypedSoftBatchProgress = Progress;
        }
    );
    TestFalse(TEXT("驻留软引用模板批次短路返回空句柄"), TypedBatchHandle.IsValid());
    TestTrue(TEXT("驻留软引用模板批次立即触发完成回调"), bTypedSoftBatchCalled);
    TestEqual(TEXT("驻留软引用模板批次进度汇报为 1.0"), TypedSoftBatchProgress, 1.0f);
    ResidentTarget->MarkAsGarbage();

    FCancellationTokenSource InFlightSource;
    bool bInFlightCallbackInvoked = false;
    FGameAssetLoadHandle InFlightHandle = FGameAssetLoader::LoadBatchAsync(
        {FSoftObjectPath(TEXT("/Game/UnitTest_UnloadedMesh_Pending.UnitTest_UnloadedMesh_Pending"))},
        DummyOwner,
        [&bInFlightCallbackInvoked]() {
            bInFlightCallbackInvoked = true;
        },
        nullptr,
        InFlightSource.GetToken()
    );
    TestTrue(TEXT("在途加载有效建立句柄"), InFlightHandle.IsValid());
    InFlightSource.Cancel();
    TestFalse(TEXT("令牌触发取消后句柄转为非加载中"), InFlightHandle.IsLoading());
    TestFalse(TEXT("中途取消不触发完成回调"), bInFlightCallbackInvoked);

    DummyOwner->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetLoaderCoroAndTokenTest,
    "Game.Asset.AssetLoader.CoroAndToken",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证单资产带取消令牌的异步加载以及 C++20 协程加载驻留资产/取消任务
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetLoaderCoroAndTokenTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* DummyOwner = NewObject<UGameInstance>();
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();

    FCancellationTokenSource CancelledSource;
    CancelledSource.Cancel();
    bool bCancelledAssetCalled = false;
    const TSoftObjectPtr<UGameInstance> ResidentSoftPtr(ResidentTarget);
    const FGameAssetLoadHandle AbortedHandle = FGameAssetLoader::LoadAsync<UGameInstance>(
        ResidentSoftPtr,
        DummyOwner,
        [&bCancelledAssetCalled](UGameInstance*) { bCancelledAssetCalled = true; },
        CancelledSource.GetToken()
    );
    TestFalse(TEXT("已取消令牌拦截单资产异步加载"), AbortedHandle.IsValid());
    TestFalse(TEXT("已取消令牌不触发单资产加载回调"), bCancelledAssetCalled);

    bool bCancelledClassCalled = false;
    const TSoftClassPtr<UGameInstance> ResidentClassPtr(UGameInstance::StaticClass());
    const FGameAssetLoadHandle AbortedClassHandle = FGameAssetLoader::LoadClassAsync<UGameInstance>(
        ResidentClassPtr,
        DummyOwner,
        [&bCancelledClassCalled](UClass*) { bCancelledClassCalled = true; },
        CancelledSource.GetToken()
    );
    TestFalse(TEXT("已取消令牌拦截单类异步加载"), AbortedClassHandle.IsValid());
    TestFalse(TEXT("已取消令牌不触发单类加载回调"), bCancelledClassCalled);

    TGameCoroTask<UGameInstance*> CoroTask = FGameAssetLoader::LoadAsyncCoro<UGameInstance>(
        ResidentSoftPtr,
        DummyOwner
    );
    TestTrue(TEXT("驻留资产协程任务立即完成"), CoroTask.IsDone());
    TestTrue(TEXT("驻留资产协程任务成功"), CoroTask.GetResult().IsSuccess());
    TestEqual(TEXT("驻留资产协程返回正确实例"), CoroTask.GetResult().GetValue(), ResidentTarget);

    TGameCoroTask<UClass*> CoroClassTask = FGameAssetLoader::LoadClassAsyncCoro<UGameInstance>(
        ResidentClassPtr,
        DummyOwner
    );
    TestTrue(TEXT("驻留类协程任务立即完成"), CoroClassTask.IsDone());
    TestTrue(TEXT("驻留类协程任务成功"), CoroClassTask.GetResult().IsSuccess());
    TestEqual(TEXT("驻留类协程返回正确 UClass"), CoroClassTask.GetResult().GetValue(), UGameInstance::StaticClass());

    TGameCoroTask<UGameInstance*> CancelledCoroTask = FGameAssetLoader::LoadAsyncCoro<UGameInstance>(
        ResidentSoftPtr,
        DummyOwner,
        CancelledSource.GetToken()
    );
    TestTrue(TEXT("已取消令牌协程任务立即完成"), CancelledCoroTask.IsDone());
    TestTrue(TEXT("已取消令牌协程任务状态为失败或取消"), CancelledCoroTask.GetResult().IsFailure() || CancelledCoroTask.IsCancelled());

    DummyOwner->MarkAsGarbage();
    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetLoaderCoroBatchTest,
    "Game.Asset.AssetLoader.CoroBatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameAssetLoader::LoadBatchAsyncCoro C++20 协程批量异步加载原语
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetLoaderCoroBatchTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* DummyOwner = NewObject<UGameInstance>();
    UGameInstance* ResidentTarget1 = NewObject<UGameInstance>();
    UGameInstance* ResidentTarget2 = NewObject<UGameInstance>();

    TGameCoroTask<TArray<UGameInstance*>> EmptyBatchTask = FGameAssetLoader::LoadBatchAsyncCoro<UGameInstance>(
        {},
        DummyOwner
    );
    TestTrue(TEXT("空数组协程任务立即完成"), EmptyBatchTask.IsDone());
    TestTrue(TEXT("空数组协程任务成功"), EmptyBatchTask.GetResult().IsSuccess());
    TestEqual(TEXT("空数组协程返回空结果"), EmptyBatchTask.GetResult().GetValue().Num(), 0);

    const TArray<TSoftObjectPtr<UGameInstance>> Targets = {
        TSoftObjectPtr<UGameInstance>(ResidentTarget1),
        TSoftObjectPtr<UGameInstance>(ResidentTarget2)
    };

    TGameCoroTask<TArray<UGameInstance*>> ResidentBatchTask = FGameAssetLoader::LoadBatchAsyncCoro<UGameInstance>(
        Targets,
        DummyOwner
    );
    TestTrue(TEXT("驻留资产批次协程立即完成"), ResidentBatchTask.IsDone());
    TestTrue(TEXT("驻留资产批次协程返回成功"), ResidentBatchTask.GetResult().IsSuccess());
    const TArray<UGameInstance*> Results = ResidentBatchTask.GetResult().GetValue();
    TestEqual(TEXT("驻留资产批次返回 2 项"), Results.Num(), 2);
    if (Results.Num() == 2) {
        TestEqual(TEXT("第 1 项实例匹配"), Results[0], ResidentTarget1);
        TestEqual(TEXT("第 2 项实例匹配"), Results[1], ResidentTarget2);
    }

    const TArray<TSoftObjectPtr<UGameInstance>> TargetsWithNull = {
        TSoftObjectPtr<UGameInstance>(ResidentTarget1),
        TSoftObjectPtr<UGameInstance>()
    };
    TGameCoroTask<TArray<UGameInstance*>> NullBatchTask = FGameAssetLoader::LoadBatchAsyncCoro<UGameInstance>(
        TargetsWithNull,
        DummyOwner
    );
    TestTrue(TEXT("含空软引用的协程任务立即完成"), NullBatchTask.IsDone());
    TestTrue(TEXT("含空软引用的协程任务成功返回"), NullBatchTask.GetResult().IsSuccess());
    const TArray<UGameInstance*> NullResults = NullBatchTask.GetResult().GetValue();
    TestEqual(TEXT("含空软引用的协程返回 2 项"), NullResults.Num(), 2);
    if (NullResults.Num() == 2) {
        TestEqual(TEXT("第 1 项为有效实例"), NullResults[0], ResidentTarget1);
        TestNull(TEXT("第 2 项空软引用对齐为 nullptr"), NullResults[1]);
    }

    FCancellationTokenSource CancelledSource;
    CancelledSource.Cancel();
    TGameCoroTask<TArray<UGameInstance*>> CancelledBatchTask = FGameAssetLoader::LoadBatchAsyncCoro<UGameInstance>(
        Targets,
        DummyOwner,
        CancelledSource.GetToken()
    );
    TestTrue(TEXT("预先取消令牌的批次协程立即完成"), CancelledBatchTask.IsDone());
    TestTrue(TEXT("预先取消令牌的批次协程状态为取消或失败"), CancelledBatchTask.IsCancelled() || CancelledBatchTask.GetResult().IsFailure());

    DummyOwner->MarkAsGarbage();
    ResidentTarget1->MarkAsGarbage();
    ResidentTarget2->MarkAsGarbage();
    return true;
}

#endif
