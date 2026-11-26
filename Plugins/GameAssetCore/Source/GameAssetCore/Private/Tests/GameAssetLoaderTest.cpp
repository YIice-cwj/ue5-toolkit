/**
 * @file GameAssetLoaderTest.cpp
 * @brief FGameAssetLoadHandle 与 FGameAssetLoader 异步流式、同步阻塞、类契约校验、跨线程取消及批次去重自动化测试
 */

#include "Loader/FGameAssetLoader.h"

#include "Async/Async.h"
#include "Async/FCancellationTokenSource.h"
#include "Async/FGameThreadTaskRunner.h"
#include "Core/AssetErrorCodes.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace {

struct FLoaderLatentTestState {
    TStrongObjectPtr<UGameInstance> AliveOwner;
    FGameAssetLoadHandle ActiveHandle;
    UStaticMesh* LoadedMesh = nullptr;
    TArray<UStaticMesh*> LoadedMeshes;
    float LastProgress = 0.0f;
    float MaxProgressReported = -1.0f;
    bool bCallbackInvoked = false;
    int32 PostCompleteFrames = 0;
    int32 FramesWaited = 0;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FWaitForGameAssetDiskLoadCommand,
    FAutomationTestBase&,
    TestRunner,
    TSharedRef<FLoaderLatentTestState>,
    SharedState
);

bool FWaitForGameAssetDiskLoadCommand::Update() {
    if (SharedState->bCallbackInvoked) {
        TestRunner.TestNotNull(TEXT("磁盘异步加载成功解析出 UStaticMesh 实例"), SharedState->LoadedMesh);
        TestRunner.TestTrue(TEXT("加载完成后句柄 HasCompleted 为真"), SharedState->ActiveHandle.HasCompleted());
        TestRunner.TestFalse(TEXT("加载完成后句柄 WasCanceled 为假"), SharedState->ActiveHandle.WasCanceled());
        TestRunner.TestEqual(TEXT("加载完成后进度为 1.0"), SharedState->ActiveHandle.GetProgress(), 1.0f);
        TestRunner.TestEqual(
            TEXT("句柄强类型 GetLoadedAsset<UStaticMesh> 与回调结果一致"),
            SharedState->ActiveHandle.GetLoadedAsset<UStaticMesh>(),
            SharedState->LoadedMesh
        );
        SharedState->ActiveHandle.Reset();
        TestRunner.TestFalse(TEXT("Reset 释放后句柄变为无效"), SharedState->ActiveHandle.IsValid());
        SharedState->AliveOwner.Reset();
        return true;
    }

    ++SharedState->FramesWaited;
    if (SharedState->FramesWaited > 300) {
        TestRunner.AddError(TEXT("等待磁盘异步资产加载回调超时"));
        SharedState->AliveOwner.Reset();
        return true;
    }
    return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FWaitForDyingOwnerGuardCommand,
    FAutomationTestBase&,
    TestRunner,
    TSharedRef<FLoaderLatentTestState>,
    SharedState
);

bool FWaitForDyingOwnerGuardCommand::Update() {
    if (!SharedState->ActiveHandle.IsLoading()) {
        ++SharedState->PostCompleteFrames;
        if (SharedState->PostCompleteFrames >= 3) {
            TestRunner.TestFalse(TEXT("宿主在完成前销毁后跨越延迟委托帧依然拦截回调"), SharedState->bCallbackInvoked);
            SharedState->ActiveHandle.Reset();
            return true;
        }
    }

    ++SharedState->FramesWaited;
    if (SharedState->FramesWaited > 300) {
        TestRunner.AddError(TEXT("等待死亡宿主保护流式句柄完结超时"));
        SharedState->ActiveHandle.Reset();
        return true;
    }
    return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FWaitForDiskBatchLoadCommand,
    FAutomationTestBase&,
    TestRunner,
    TSharedRef<FLoaderLatentTestState>,
    SharedState
);

bool FWaitForDiskBatchLoadCommand::Update() {
    if (SharedState->bCallbackInvoked) {
        TestRunner.TestEqual(TEXT("磁盘强类型批次输出 3 项"), SharedState->LoadedMeshes.Num(), 3);
        TestRunner.TestEqual(TEXT("成功磁盘批次上报进度 1.0"), SharedState->LastProgress, 1.0f);
        if (SharedState->LoadedMeshes.Num() == 3) {
            TestRunner.TestNotNull(TEXT("槽位 0 Cone 网格加载有效"), SharedState->LoadedMeshes[0]);
            TestRunner.TestNull(TEXT("槽位 1 空项保持 nullptr"), SharedState->LoadedMeshes[1]);
            TestRunner.TestEqual(TEXT("槽位 2 重复项与槽位 0 指向同一实例"), SharedState->LoadedMeshes[2], SharedState->LoadedMeshes[0]);
        }
        if (SharedState->ActiveHandle.HasCompleted()) {
            TArray<UStaticMesh*> HandleMeshes;
            SharedState->ActiveHandle.GetLoadedAssets<UStaticMesh>(HandleMeshes);
            TestRunner.TestEqual(TEXT("底层句柄去重后实际仅流送 1 个唯一网格"), HandleMeshes.Num(), 1);
        }
        SharedState->ActiveHandle.Reset();
        SharedState->AliveOwner.Reset();
        return true;
    }

    ++SharedState->FramesWaited;
    if (SharedState->FramesWaited > 300) {
        TestRunner.AddError(TEXT("等待磁盘强类型批量异步加载回调超时"));
        SharedState->ActiveHandle.Reset();
        SharedState->AliveOwner.Reset();
        return true;
    }
    return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FWaitForMissingBatchProgressCheckCommand,
    FAutomationTestBase&,
    TestRunner,
    TSharedRef<FLoaderLatentTestState>,
    SharedState
);

bool FWaitForMissingBatchProgressCheckCommand::Update() {
    if (SharedState->bCallbackInvoked) {
        TestRunner.TestTrue(
            TEXT("缺失资产导致失败的批次绝不误报 OnProgress(1.0f)"),
            SharedState->MaxProgressReported < 1.0f
        );
        SharedState->ActiveHandle.Reset();
        SharedState->AliveOwner.Reset();
        return true;
    }

    ++SharedState->FramesWaited;
    if (SharedState->FramesWaited > 300) {
        TestRunner.AddError(TEXT("等待缺失批次异步回调超时"));
        SharedState->ActiveHandle.Reset();
        SharedState->AliveOwner.Reset();
        return true;
    }
    return false;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetHandleLifecycleTest,
    "Game.Asset.Loader.HandleLifecycleAndCancel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameAssetLoadHandle 默认态、句柄主动取消与令牌自动解绑、跨工作线程取消及 Reset 释放
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetHandleLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameAssetLoadHandle EmptyHandle;
    TestFalse(TEXT("默认句柄 IsValid 为假"), EmptyHandle.IsValid());
    TestFalse(TEXT("默认句柄 operator bool 为假"), static_cast<bool>(EmptyHandle));
    TestFalse(TEXT("默认句柄 IsLoading 为假"), EmptyHandle.IsLoading());
    TestFalse(TEXT("默认句柄 HasCompleted 为假"), EmptyHandle.HasCompleted());
    TestFalse(TEXT("默认句柄 HasError 为假"), EmptyHandle.HasError());
    TestFalse(TEXT("默认句柄 WasCanceled 为假"), EmptyHandle.WasCanceled());
    TestEqual(TEXT("默认句柄进度为 0.0"), EmptyHandle.GetProgress(), 0.0f);
    TestNull(TEXT("默认句柄 GetLoadedAsset 返回 nullptr"), EmptyHandle.GetLoadedAsset());

    TArray<UObject*> EmptyList;
    EmptyHandle.GetLoadedAssets(EmptyList);
    TestEqual(TEXT("默认句柄 GetLoadedAssets 为空数组"), EmptyList.Num(), 0);
    EmptyHandle.Reset();
    EmptyHandle.Cancel();

    UGameInstance* Owner = NewObject<UGameInstance>();
    FCancellationTokenSource SharedLongLivedSource;
    TSharedRef<bool> bCancelledCallbackInvoked = MakeShared<bool>(false);
    const TArray<FSoftObjectPath> PendingPaths = {
        FSoftObjectPath(TEXT("/Game/__UnitTest_HandleCancel_PendingAsset.__UnitTest_HandleCancel_PendingAsset"))
    };

    FGameAssetLoadHandle ActiveHandle = FGameAssetLoader::LoadBatchAsync(
        PendingPaths,
        Owner,
        [bCancelledCallbackInvoked]() {
            *bCancelledCallbackInvoked = true;
        },
        nullptr,
        SharedLongLivedSource.GetToken()
    );
    TestTrue(TEXT("确定未驻留路径建立活跃流式句柄"), ActiveHandle.IsValid());
    TestTrue(TEXT("活跃句柄布尔转换为真"), static_cast<bool>(ActiveHandle));

    ActiveHandle.Cancel();
    TestFalse(TEXT("Cancel 后句柄立即变为非活跃"), ActiveHandle.IsValid());
    TestTrue(TEXT("Cancel 后句柄保留 WasCanceled 为真"), ActiveHandle.WasCanceled());
    TestFalse(TEXT("Cancel 后句柄 HasCompleted 为假"), ActiveHandle.HasCompleted());
    TestFalse(TEXT("Cancel 后句柄 IsLoading 为假"), ActiveHandle.IsLoading());
    TestEqual(TEXT("Cancel 后句柄进度归零"), ActiveHandle.GetProgress(), 0.0f);
    TestNull(TEXT("Cancel 后 GetLoadedAsset 返回 nullptr"), ActiveHandle.GetLoadedAsset());

    SharedLongLivedSource.Cancel();
    FlushAsyncLoading();
    TestFalse(TEXT("已取消句柄在令牌取消与 FlushAsyncLoading 后绝不触发完成回调"), *bCancelledCallbackInvoked);

    ActiveHandle.Reset();
    TestFalse(TEXT("Reset 清空后 WasCanceled 复位为假"), ActiveHandle.WasCanceled());

    FCancellationTokenSource WorkerCancelSource;
    TSharedRef<bool> bWorkerCancelledCallbackInvoked = MakeShared<bool>(false);
    FGameAssetLoadHandle WorkerCancelHandle = FGameAssetLoader::LoadBatchAsync(
        {FSoftObjectPath(TEXT("/Game/__UnitTest_WorkerCancelBatch.__UnitTest_WorkerCancelBatch"))},
        Owner,
        [bWorkerCancelledCallbackInvoked]() {
            *bWorkerCancelledCallbackInvoked = true;
        },
        nullptr,
        WorkerCancelSource.GetToken()
    );
    TestTrue(TEXT("工作线程取消测试成功建立活跃句柄"), WorkerCancelHandle.IsValid());
    TFuture<void> CancelFuture = Async(EAsyncExecution::ThreadPool, [&WorkerCancelSource]() {
        WorkerCancelSource.Cancel();
    });
    CancelFuture.Wait();
    FGameThreadTaskRunner::FlushInFlightTasks();
    TestTrue(TEXT("工作线程触发取消令牌后句柄在 GameThread 正确转为 WasCanceled"), WorkerCancelHandle.WasCanceled());
    TestFalse(TEXT("工作线程取消后绝不触发完成回调"), *bWorkerCancelledCallbackInvoked);
    WorkerCancelHandle.Reset();

    Owner->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetLoaderGuardsAndTypeCheckTest,
    "Game.Asset.Loader.AsyncGuardsShortCircuitAndTypes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证单资产与单类异步加载的空守卫、驻留短路、跨基类类型不匹配拦截与延迟帧死亡宿主防护
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetLoaderGuardsAndTypeCheckTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* Owner = NewObject<UGameInstance>();
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();

    bool bNullAssetCalled = false;
    const FGameAssetLoadHandle NullHandle = FGameAssetLoader::LoadAsync<UGameInstance>(
        TSoftObjectPtr<UGameInstance>(),
        Owner,
        [&bNullAssetCalled](UGameInstance*) {
            bNullAssetCalled = true;
        }
    );
    TestFalse(TEXT("空软引用返回无效句柄"), NullHandle.IsValid());
    TestFalse(TEXT("空软引用不触发回调"), bNullAssetCalled);

    bool bDeadOwnerCalled = false;
    const TSoftObjectPtr<UGameInstance> ResidentSoftPtr(ResidentTarget);
    const FGameAssetLoadHandle DeadOwnerHandle = FGameAssetLoader::LoadAsync<UGameInstance>(
        ResidentSoftPtr,
        nullptr,
        [&bDeadOwnerCalled](UGameInstance*) {
            bDeadOwnerCalled = true;
        }
    );
    TestFalse(TEXT("空宿主返回无效句柄"), DeadOwnerHandle.IsValid());
    TestFalse(TEXT("空宿主绝不触发回调"), bDeadOwnerCalled);

    UGameInstance* ResolvedInstance = nullptr;
    const FGameAssetLoadHandle ResidentHandle = FGameAssetLoader::LoadAsync<UGameInstance>(
        ResidentSoftPtr,
        Owner,
        [&ResolvedInstance](UGameInstance* Loaded) {
            ResolvedInstance = Loaded;
        }
    );
    TestFalse(TEXT("已驻留资产同步短路无需保留句柄"), ResidentHandle.IsValid());
    TestEqual(TEXT("已驻留资产同步传回精确实例"), ResolvedInstance, ResidentTarget);

    bool bMismatchAssetCallbackInvoked = false;
    UStaticMesh* MismatchedMeshResult = reinterpret_cast<UStaticMesh*>(0x1);
    const TSoftObjectPtr<UStaticMesh> WrongTypeAssetPtr{FSoftObjectPath(ResidentTarget)};
    FGameAssetLoader::LoadAsync<UStaticMesh>(
        WrongTypeAssetPtr,
        Owner,
        [&bMismatchAssetCallbackInvoked, &MismatchedMeshResult](UStaticMesh* LoadedMesh) {
            bMismatchAssetCallbackInvoked = true;
            MismatchedMeshResult = LoadedMesh;
        }
    );
    TestTrue(TEXT("驻留类型不匹配资产仍触发结果回调通知"), bMismatchAssetCallbackInvoked);
    TestNull(TEXT("驻留类型不匹配资产必须向回调传入 nullptr"), MismatchedMeshResult);

    TSubclassOf<UGameInstance> LoadedValidClass;
    FGameAssetLoader::LoadClassAsync<UGameInstance>(
        TSoftClassPtr<UGameInstance>(UGameInstance::StaticClass()),
        Owner,
        [&LoadedValidClass](TSubclassOf<UGameInstance> Cls) {
            LoadedValidClass = Cls;
        }
    );
    TestEqual(TEXT("兼容基类的软类引用正确返回 UGameInstance::StaticClass()"), LoadedValidClass.Get(), UGameInstance::StaticClass());

    bool bWrongBaseClassCalled = false;
    UClass* WrongBaseClassPtr = UGameInstance::StaticClass();
    const TSoftClassPtr<AActor> WrongHierarchySoftClass{FSoftObjectPath(UGameInstance::StaticClass())};
    FGameAssetLoader::LoadClassAsync<AActor>(
        WrongHierarchySoftClass,
        Owner,
        [&bWrongBaseClassCalled, &WrongBaseClassPtr](UClass* Cls) {
            bWrongBaseClassCalled = true;
            WrongBaseClassPtr = Cls;
        }
    );
    TestTrue(TEXT("跨继承树不兼容类触发结果回调"), bWrongBaseClassCalled);
    TestNull(TEXT("UGameInstance 非 AActor 子类，LoadClassAsync<AActor> 必须传入 nullptr"), WrongBaseClassPtr);

    TSharedRef<FLoaderLatentTestState> DyingState = MakeShared<FLoaderLatentTestState>();
    UGameInstance* DyingOwner = NewObject<UGameInstance>();
    DyingState->ActiveHandle = FGameAssetLoader::LoadAsync<UStaticMesh>(
        TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/__UnitTest_DyingOwner_Mesh.__UnitTest_DyingOwner_Mesh"))),
        DyingOwner,
        [DyingState](UStaticMesh*) {
            DyingState->bCallbackInvoked = true;
        }
    );
    DyingOwner->MarkAsGarbage();
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForDyingOwnerGuardCommand(*this, DyingState));

    Owner->MarkAsGarbage();
    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetLoaderSyncAndErrorCodesTest,
    "Game.Asset.Loader.SyncLoadAndErrorCodes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证同步加载全错误码分支（3001 空路径 / 3002 加载失败 / 3003 类型不匹配 / 3005 非游戏线程拦截）
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetLoaderSyncAndErrorCodesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();

    const TGameStatusResult<UGameInstance*> NullAssetRes = FGameAssetLoader::LoadSync<UGameInstance>(TSoftObjectPtr<UGameInstance>());
    TestTrue(TEXT("空软引用同步加载失败"), NullAssetRes.IsFailure());
    TestEqual(TEXT("空软引用模块 ID 为 Asset"), NullAssetRes.GetError().ModuleId, Game::ModuleId::Asset);
    TestEqual(TEXT("空软引用错误码为 InvalidAssetPath (3001)"), NullAssetRes.GetError().ErrorCode, Game::AssetErrorCodes::InvalidAssetPath);

    const TGameStatusResult<UGameInstance*> ResidentRes = FGameAssetLoader::LoadSync<UGameInstance>(TSoftObjectPtr<UGameInstance>(ResidentTarget));
    TestTrue(TEXT("已驻留资产同步加载成功"), ResidentRes.IsSuccess());
    TestEqual(TEXT("已驻留资产返回正确实例"), ResidentRes.GetValue(), ResidentTarget);

    const TGameStatusResult<UStaticMesh*> CubeRes = FGameAssetLoader::LoadSync<UStaticMesh>(
        TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")))
    );
    TestTrue(TEXT("引擎磁盘资产 Cube 同步加载成功"), CubeRes.IsSuccess());
    TestNotNull(TEXT("引擎磁盘资产 Cube 非空"), CubeRes.GetValue());

    const TGameStatusResult<UMaterialInterface*> MismatchAssetRes = FGameAssetLoader::LoadSync<UMaterialInterface>(
        TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")))
    );
    TestTrue(TEXT("将 UStaticMesh 当作 UMaterialInterface 加载必须返回失败"), MismatchAssetRes.IsFailure());
    TestEqual(TEXT("资产类型不匹配返回 TypeMismatch (3003)"), MismatchAssetRes.GetError().ErrorCode, Game::AssetErrorCodes::TypeMismatch);

    const TGameStatusResult<UStaticMesh*> MissingAssetRes = FGameAssetLoader::LoadSync<UStaticMesh>(
        TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/__UnitTest_NonExistent_Asset.__UnitTest_NonExistent_Asset")))
    );
    TestTrue(TEXT("不存在的磁盘路径同步加载失败"), MissingAssetRes.IsFailure());
    TestEqual(TEXT("不存在路径返回 LoadFailed (3002)"), MissingAssetRes.GetError().ErrorCode, Game::AssetErrorCodes::LoadFailed);

    const TGameStatusResult<TSubclassOf<UGameInstance>> NullClassRes =
        FGameAssetLoader::LoadClassSync<UGameInstance>(TSoftClassPtr<UGameInstance>());
    TestTrue(TEXT("空软类引用同步加载失败"), NullClassRes.IsFailure());
    TestEqual(TEXT("空软类引用错误码为 3001"), NullClassRes.GetError().ErrorCode, Game::AssetErrorCodes::InvalidAssetPath);

    const TGameStatusResult<TSubclassOf<UGameInstance>> ValidClassRes =
        FGameAssetLoader::LoadClassSync<UGameInstance>(TSoftClassPtr<UGameInstance>(UGameInstance::StaticClass()));
    TestTrue(TEXT("有效软类引用同步加载成功"), ValidClassRes.IsSuccess());
    TestEqual(TEXT("有效软类引用返回 UGameInstance::StaticClass()"), ValidClassRes.GetValue().Get(), UGameInstance::StaticClass());

    const TGameStatusResult<TSubclassOf<AActor>> MismatchClassRes =
        FGameAssetLoader::LoadClassSync<AActor>(TSoftClassPtr<AActor>(FSoftObjectPath(UGameInstance::StaticClass())));
    TestTrue(TEXT("非 AActor 子类的软类引用同步加载必须失败"), MismatchClassRes.IsFailure());
    TestEqual(TEXT("类继承契约不匹配返回 TypeMismatch (3003)"), MismatchClassRes.GetError().ErrorCode, Game::AssetErrorCodes::TypeMismatch);

    const TGameStatusResult<TSubclassOf<AActor>> NonClassObjectRes =
        FGameAssetLoader::LoadClassSync<AActor>(TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))));
    TestTrue(TEXT("指向普通资产而非 UClass 的软类引用同步加载必须失败"), NonClassObjectRes.IsFailure());
    TestEqual(TEXT("非 UClass 对象返回 TypeMismatch (3003)"), NonClassObjectRes.GetError().ErrorCode, Game::AssetErrorCodes::TypeMismatch);

    TFuture<int32> WorkerErrorCodeFuture = Async(EAsyncExecution::ThreadPool, []() {
        const TGameStatusResult<UStaticMesh*> BgRes = FGameAssetLoader::LoadSync<UStaticMesh>(
            TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/__UnitTest_WorkerSyncLoad.__UnitTest_WorkerSyncLoad")))
        );
        return BgRes.IsFailure() ? BgRes.GetError().ErrorCode : 0;
    });
    TestEqual(TEXT("后台线程请求未驻留资产同步加载返回 InvalidThread (3005)"), WorkerErrorCodeFuture.Get(), Game::AssetErrorCodes::InvalidThread);

    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetLoaderBatchTest,
    "Game.Asset.Loader.BatchTypedAndUntyped",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证路径级与强类型模板级批量加载在去重、空项顺序对齐、取消令牌联动、失败进度过滤与真实磁盘流送上的契约
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetLoaderBatchTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* Owner = NewObject<UGameInstance>();
    UGameInstance* Resident1 = NewObject<UGameInstance>();
    UGameInstance* Resident2 = NewObject<UGameInstance>();

    FCancellationTokenSource PreCancelledSource;
    PreCancelledSource.Cancel();
    bool bPreCancelledCalled = false;
    const FGameAssetLoadHandle PreCancelledHandle = FGameAssetLoader::LoadBatchAsync(
        {FSoftObjectPath(TEXT("/Engine/BasicShapes/Cone.Cone"))},
        Owner,
        [&bPreCancelledCalled]() {
            bPreCancelledCalled = true;
        },
        nullptr,
        PreCancelledSource.GetToken()
    );
    TestFalse(TEXT("预取消令牌返回空句柄"), PreCancelledHandle.IsValid());
    TestFalse(TEXT("预取消令牌不触发回调"), bPreCancelledCalled);

    bool bTypedImmediateCalled = false;
    float TypedImmediateProgress = 0.0f;
    TArray<UGameInstance*> DeliveredTypedBatch;
    const TArray<TSoftObjectPtr<UGameInstance>> MixedResidentBatch = {
        TSoftObjectPtr<UGameInstance>(Resident1),
        TSoftObjectPtr<UGameInstance>(),
        TSoftObjectPtr<UGameInstance>(Resident2),
        TSoftObjectPtr<UGameInstance>(Resident1)
    };
    const FGameAssetLoadHandle TypedResidentHandle = FGameAssetLoader::LoadBatchAsync<UGameInstance>(
        MixedResidentBatch,
        Owner,
        [&bTypedImmediateCalled, &DeliveredTypedBatch](const TArray<UGameInstance*>& LoadedList) {
            bTypedImmediateCalled = true;
            DeliveredTypedBatch = LoadedList;
        },
        [&TypedImmediateProgress](float Progress) {
            TypedImmediateProgress = Progress;
        }
    );
    TestFalse(TEXT("全驻留强类型批次走同步短路返回空句柄"), TypedResidentHandle.IsValid());
    TestTrue(TEXT("全驻留强类型批次同步触发回调"), bTypedImmediateCalled);
    TestEqual(TEXT("全驻留强类型批次汇报进度 1.0"), TypedImmediateProgress, 1.0f);
    TestEqual(TEXT("交付数组与输入长度严格一致（4 项）"), DeliveredTypedBatch.Num(), 4);
    if (DeliveredTypedBatch.Num() == 4) {
        TestEqual(TEXT("槽位 0 对齐 Resident1"), DeliveredTypedBatch[0], Resident1);
        TestNull(TEXT("槽位 1 空引用对齐 nullptr"), DeliveredTypedBatch[1]);
        TestEqual(TEXT("槽位 2 对齐 Resident2"), DeliveredTypedBatch[2], Resident2);
        TestEqual(TEXT("槽位 3 重复项对齐 Resident1"), DeliveredTypedBatch[3], Resident1);
    }

    FCancellationTokenSource InFlightSource;
    TSharedRef<bool> bInFlightBatchCalled = MakeShared<bool>(false);
    FGameAssetLoadHandle InFlightHandle = FGameAssetLoader::LoadBatchAsync(
        {FSoftObjectPath(TEXT("/Game/__UnitTest_InFlightTokenCancel.__UnitTest_InFlightTokenCancel"))},
        Owner,
        [bInFlightBatchCalled]() {
            *bInFlightBatchCalled = true;
        },
        nullptr,
        InFlightSource.GetToken()
    );
    TestTrue(TEXT("在途请求有效建立句柄"), InFlightHandle.IsValid());
    InFlightSource.Cancel();
    TestTrue(TEXT("取消令牌触发后句柄标记 WasCanceled"), InFlightHandle.WasCanceled());
    FlushAsyncLoading();
    TestFalse(TEXT("令牌中途取消后绝不触发完成回调"), *bInFlightBatchCalled);

    TSharedRef<FLoaderLatentTestState> DiskBatchState = MakeShared<FLoaderLatentTestState>();
    DiskBatchState->AliveOwner = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>());
    const FSoftObjectPath ConePath(TEXT("/Engine/BasicShapes/Cone.Cone"));
    const TArray<TSoftObjectPtr<UStaticMesh>> DiskBatchInput = {
        TSoftObjectPtr<UStaticMesh>(ConePath),
        TSoftObjectPtr<UStaticMesh>(),
        TSoftObjectPtr<UStaticMesh>(ConePath)
    };
    DiskBatchState->ActiveHandle = FGameAssetLoader::LoadBatchAsync<UStaticMesh>(
        DiskBatchInput,
        DiskBatchState->AliveOwner.Get(),
        [DiskBatchState](const TArray<UStaticMesh*>& LoadedMeshes) {
            DiskBatchState->bCallbackInvoked = true;
            DiskBatchState->LoadedMeshes = LoadedMeshes;
        },
        [DiskBatchState](float Progress) {
            DiskBatchState->LastProgress = Progress;
        }
    );
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForDiskBatchLoadCommand(*this, DiskBatchState));

    TSharedRef<FLoaderLatentTestState> MissingBatchState = MakeShared<FLoaderLatentTestState>();
    MissingBatchState->AliveOwner = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>());
    MissingBatchState->ActiveHandle = FGameAssetLoader::LoadBatchAsync(
        {FSoftObjectPath(TEXT("/Game/__UnitTest_BatchMissingProgress.__UnitTest_BatchMissingProgress"))},
        MissingBatchState->AliveOwner.Get(),
        [MissingBatchState]() {
            MissingBatchState->bCallbackInvoked = true;
        },
        [MissingBatchState](float Progress) {
            MissingBatchState->MaxProgressReported = FMath::Max(MissingBatchState->MaxProgressReported, Progress);
        }
    );
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForMissingBatchProgressCheckCommand(*this, MissingBatchState));

    Owner->MarkAsGarbage();
    Resident1->MarkAsGarbage();
    Resident2->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetLoaderDiskLatentTest,
    "Game.Asset.Loader.DiskStreamingLatent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 通过引擎 Latent 帧循环验证真实引擎网格资产流式加载与句柄提取（零 Sleep 盲等）
 * @param Parameters 自动化测试参数
 * @return Latent 命令挂载或短路断言成功返回 true
 */
bool FGameAssetLoaderDiskLatentTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSharedRef<FLoaderLatentTestState> State = MakeShared<FLoaderLatentTestState>();
    State->AliveOwner = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>());

    const TSoftObjectPtr<UStaticMesh> SphereSoft(FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    State->ActiveHandle = FGameAssetLoader::LoadAsync<UStaticMesh>(
        SphereSoft,
        State->AliveOwner.Get(),
        [State](UStaticMesh* Loaded) {
            State->LoadedMesh = Loaded;
            State->bCallbackInvoked = true;
        },
        FCancellationToken::None(),
        EGameAssetPriority::High
    );

    if (State->bCallbackInvoked) {
        TestNotNull(TEXT("资产若已缓存于内存亦正确返回 UStaticMesh"), State->LoadedMesh);
        State->AliveOwner.Reset();
        return true;
    }

    TestTrue(TEXT("未驻留资产返回有效异步加载句柄"), State->ActiveHandle.IsValid());
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForGameAssetDiskLoadCommand(*this, State));
    return true;
}

#endif
