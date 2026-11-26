/**
 * @file GameAssetCoroTest.cpp
 * @brief FGameAssetLoader 事件驱动 C++20 协程加载原语（无 UWorld 宿主磁盘单/批量流送、值传参防悬垂与跨线程取消）测试
 */

#include "Loader/FGameAssetLoader.h"

#include "Async/Async.h"
#include "Async/FCancellationTokenSource.h"
#include "Async/FGameThreadTaskRunner.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace {

struct FCoroLatentTestState {
    TStrongObjectPtr<UGameInstance> AliveOwner;
    TOptional<TGameCoroTask<UStaticMesh*>> SingleTask;
    TOptional<TGameCoroTask<TArray<UStaticMesh*>>> BatchTask;
    int32 FramesWaited = 0;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FWaitForCoroDiskSingleCommand,
    FAutomationTestBase&,
    TestRunner,
    TSharedRef<FCoroLatentTestState>,
    SharedState
);

bool FWaitForCoroDiskSingleCommand::Update() {
    if (SharedState->SingleTask.IsSet() && SharedState->SingleTask->IsDone()) {
        TestRunner.TestTrue(TEXT("真实磁盘单资产协程进入 Completed 终态"), SharedState->SingleTask->IsCompleted());
        const TGameStatusResult<UStaticMesh*> Result = SharedState->SingleTask->GetResult();
        TestRunner.TestTrue(TEXT("真实磁盘单资产协程结果成功"), Result.IsSuccess());
        if (Result.IsSuccess()) {
            TestRunner.TestNotNull(TEXT("真实磁盘单资产协程解析出 Cylinder UStaticMesh"), Result.GetValue());
        }
        SharedState->AliveOwner.Reset();
        return true;
    }

    ++SharedState->FramesWaited;
    if (SharedState->FramesWaited > 300) {
        TestRunner.AddError(TEXT("等待真实磁盘单资产协程完成超时"));
        SharedState->AliveOwner.Reset();
        return true;
    }
    return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FWaitForCoroDiskBatchCommand,
    FAutomationTestBase&,
    TestRunner,
    TSharedRef<FCoroLatentTestState>,
    SharedState
);

bool FWaitForCoroDiskBatchCommand::Update() {
    if (SharedState->BatchTask.IsSet() && SharedState->BatchTask->IsDone()) {
        TestRunner.TestTrue(TEXT("临时列表入参的真实磁盘批量协程进入 Completed 终态"), SharedState->BatchTask->IsCompleted());
        const TGameStatusResult<TArray<UStaticMesh*>> Result = SharedState->BatchTask->GetResult();
        TestRunner.TestTrue(TEXT("真实磁盘批量协程结果成功"), Result.IsSuccess());
        if (Result.IsSuccess()) {
            const TArray<UStaticMesh*>& LoadedMeshes = Result.GetValue();
            TestRunner.TestEqual(TEXT("批量协程返回与临时输入列表等长的 3 项结果"), LoadedMeshes.Num(), 3);
            if (LoadedMeshes.Num() == 3) {
                TestRunner.TestNotNull(TEXT("槽位 0 成功加载 Plane UStaticMesh"), LoadedMeshes[0]);
                TestRunner.TestNull(TEXT("槽位 1 空软引用对齐 nullptr"), LoadedMeshes[1]);
                TestRunner.TestEqual(TEXT("槽位 2 重复项与槽位 0 指向同一实例"), LoadedMeshes[2], LoadedMeshes[0]);
            }
        }
        SharedState->AliveOwner.Reset();
        return true;
    }

    ++SharedState->FramesWaited;
    if (SharedState->FramesWaited > 300) {
        TestRunner.AddError(TEXT("等待真实磁盘批量协程完成超时"));
        SharedState->AliveOwner.Reset();
        return true;
    }
    return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FWaitForCoroDyingOwnerAbortCommand,
    FAutomationTestBase&,
    TestRunner,
    TSharedRef<FCoroLatentTestState>,
    SharedState
);

bool FWaitForCoroDyingOwnerAbortCommand::Update() {
    if (SharedState->SingleTask.IsSet() && SharedState->SingleTask->IsDone()) {
        TestRunner.TestTrue(
            TEXT("挂起期间宿主销毁后流式委托唤醒时协程安全自毁为 Cancelled"),
            SharedState->SingleTask->IsCancelled()
        );
        return true;
    }

    ++SharedState->FramesWaited;
    if (SharedState->FramesWaited > 300) {
        TestRunner.AddError(TEXT("等待宿主销毁后的挂起协程终态通知超时"));
        return true;
    }
    return false;
}

TGameCoroTask<bool> RunNestedChildCancelCoro(
    UObject* InOwner,
    FCancellationToken InChildToken,
    TSharedRef<bool> OutResumedOnGameThread
) {
    co_await Game::Coro::BindContext(InOwner);
    const TGameStatusResult<UStaticMesh*> ChildResult = co_await FGameAssetLoader::LoadAsyncCoro<UStaticMesh>(
        TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/__UnitTest_CoroNestedWorkerCancel.__UnitTest_CoroNestedWorkerCancel"))),
        InOwner,
        InChildToken
    );
    *OutResumedOnGameThread = IsInGameThread();
    co_return ChildResult.IsFailure() && ChildResult.GetError().ErrorCode == Game::CommonErrorCodes::Cancelled;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetCoroSingleAndClassTest,
    "Game.Asset.Coro.SingleAndClassLoad",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证事件驱动单资产与单类协程在无 UWorld 宿主下的真实磁盘流送、驻留直返、空宿主自毁与跨继承树类型拦截
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetCoroSingleAndClassTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* OwnerWithoutWorld = NewObject<UGameInstance>();
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();

    TGameCoroTask<UGameInstance*> NullOwnerTask = FGameAssetLoader::LoadAsyncCoro<UGameInstance>(
        TSoftObjectPtr<UGameInstance>(ResidentTarget),
        nullptr
    );
    TestTrue(TEXT("空宿主协程立即中止进入终态"), NullOwnerTask.IsDone());
    TestTrue(TEXT("空宿主协程状态为 Cancelled"), NullOwnerTask.IsCancelled());

    TGameCoroTask<UGameInstance*> NullAssetTask = FGameAssetLoader::LoadAsyncCoro<UGameInstance>(
        TSoftObjectPtr<UGameInstance>(),
        OwnerWithoutWorld
    );
    TestTrue(TEXT("空软引用协程立即完成"), NullAssetTask.IsCompleted());
    TestNull(TEXT("空软引用协程返回 nullptr"), NullAssetTask.GetResult().GetValue());

    TGameCoroTask<UGameInstance*> ResidentTask = FGameAssetLoader::LoadAsyncCoro<UGameInstance>(
        TSoftObjectPtr<UGameInstance>(ResidentTarget),
        OwnerWithoutWorld
    );
    TestTrue(TEXT("驻留资产协程零挂起立即完成"), ResidentTask.IsCompleted());
    TestEqual(TEXT("驻留资产协程返回正确实例"), ResidentTask.GetResult().GetValue(), ResidentTarget);

    TGameCoroTask<UStaticMesh*> MismatchAssetTask = FGameAssetLoader::LoadAsyncCoro<UStaticMesh>(
        TSoftObjectPtr<UStaticMesh>{FSoftObjectPath(ResidentTarget)},
        OwnerWithoutWorld
    );
    TestTrue(TEXT("类型不匹配资产协程立即完成"), MismatchAssetTask.IsCompleted());
    TestNull(TEXT("类型不匹配资产协程返回 nullptr"), MismatchAssetTask.GetResult().GetValue());

    TGameCoroTask<TSubclassOf<UGameInstance>> ValidClassTask = FGameAssetLoader::LoadClassAsyncCoro<UGameInstance>(
        TSoftClassPtr<UGameInstance>(UGameInstance::StaticClass()),
        OwnerWithoutWorld
    );
    TestTrue(TEXT("有效软类协程立即完成"), ValidClassTask.IsCompleted());
    TestEqual(TEXT("有效软类协程返回 UGameInstance::StaticClass()"), ValidClassTask.GetResult().GetValue().Get(), UGameInstance::StaticClass());

    TGameCoroTask<TSubclassOf<AActor>> WrongHierarchyClassTask = FGameAssetLoader::LoadClassAsyncCoro<AActor>(
        TSoftClassPtr<AActor>{FSoftObjectPath(UGameInstance::StaticClass())},
        OwnerWithoutWorld
    );
    TestTrue(TEXT("不兼容基类的软类协程完成"), WrongHierarchyClassTask.IsCompleted());
    TestNull(TEXT("非 AActor 子类的软类协程必须返回 nullptr"), WrongHierarchyClassTask.GetResult().GetValue().Get());

    TSharedRef<FCoroLatentTestState> DiskSingleState = MakeShared<FCoroLatentTestState>();
    DiskSingleState->AliveOwner = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>());
    DiskSingleState->SingleTask.Emplace(FGameAssetLoader::LoadAsyncCoro<UStaticMesh>(
        TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"))),
        DiskSingleState->AliveOwner.Get(),
        FCancellationToken::None(),
        EGameAssetPriority::High
    ));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCoroDiskSingleCommand(*this, DiskSingleState));

    OwnerWithoutWorld->MarkAsGarbage();
    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetCoroBatchAndCancellationTest,
    "Game.Asset.Coro.BatchAndCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证批量协程临时列表按值传参的真实磁盘流送、去重顺序对齐，以及主线程与后台工作线程的中途取消与宿主销毁自毁
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetCoroBatchAndCancellationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* Owner = NewObject<UGameInstance>();
    UGameInstance* Resident1 = NewObject<UGameInstance>();

    TGameCoroTask<TArray<UGameInstance*>> EmptyTask = FGameAssetLoader::LoadBatchAsyncCoro<UGameInstance>(
        {},
        Owner
    );
    TestTrue(TEXT("空数组批量协程立即完成"), EmptyTask.IsCompleted());
    TestEqual(TEXT("空数组批量协程返回 0 项"), EmptyTask.GetResult().GetValue().Num(), 0);

    const TArray<TSoftObjectPtr<UGameInstance>> ResidentAndNullList = {
        TSoftObjectPtr<UGameInstance>(Resident1),
        TSoftObjectPtr<UGameInstance>()
    };
    TGameCoroTask<TArray<UGameInstance*>> FastBatchTask = FGameAssetLoader::LoadBatchAsyncCoro<UGameInstance>(
        ResidentAndNullList,
        Owner
    );
    TestTrue(TEXT("全驻留含空槽位的批量协程零挂起立即完成"), FastBatchTask.IsCompleted());
    const TArray<UGameInstance*> FastResults = FastBatchTask.GetResult().GetValue();
    TestEqual(TEXT("全驻留批量协程返回 2 项"), FastResults.Num(), 2);
    if (FastResults.Num() == 2) {
        TestEqual(TEXT("第 0 项匹配 Resident1"), FastResults[0], Resident1);
        TestNull(TEXT("第 1 项空槽位对齐 nullptr"), FastResults[1]);
    }

    FCancellationTokenSource MidFlightTokenSource;
    TGameCoroTask<UStaticMesh*> TokenCancelTask = FGameAssetLoader::LoadAsyncCoro<UStaticMesh>(
        TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/__UnitTest_CoroTokenCancel.__UnitTest_CoroTokenCancel"))),
        Owner,
        MidFlightTokenSource.GetToken()
    );
    TestFalse(TEXT("未驻留路径协程进入挂起等待态"), TokenCancelTask.IsDone());
    MidFlightTokenSource.Cancel();
    TestTrue(TEXT("触发 CancellationToken 后挂起协程立即转为已结束"), TokenCancelTask.IsDone());
    TestTrue(TEXT("触发 CancellationToken 后协程状态为 Cancelled"), TokenCancelTask.IsCancelled());

    TGameCoroTask<TArray<UStaticMesh*>> DirectCancelBatchTask = FGameAssetLoader::LoadBatchAsyncCoro<UStaticMesh>(
        {TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/__UnitTest_CoroDirectCancel.__UnitTest_CoroDirectCancel")))},
        Owner
    );
    TestFalse(TEXT("批量未驻留路径协程进入挂起态"), DirectCancelBatchTask.IsDone());
    TestTrue(TEXT("主动调用 Task.Cancel() 首次成功"), DirectCancelBatchTask.Cancel());
    TestTrue(TEXT("Task.Cancel() 后协程立即处于 Cancelled 终态"), DirectCancelBatchTask.IsCancelled());

    FCancellationTokenSource WorkerTokenSource;
    TGameCoroTask<UStaticMesh*> WorkerTokenCancelTask = FGameAssetLoader::LoadAsyncCoro<UStaticMesh>(
        TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/__UnitTest_CoroWorkerToken.__UnitTest_CoroWorkerToken"))),
        Owner,
        WorkerTokenSource.GetToken()
    );
    TestFalse(TEXT("工作线程令牌取消前协程处于挂起态"), WorkerTokenCancelTask.IsDone());
    TFuture<void> WorkerTokenFuture = Async(EAsyncExecution::ThreadPool, [&WorkerTokenSource]() {
        WorkerTokenSource.Cancel();
    });
    WorkerTokenFuture.Wait();
    FGameThreadTaskRunner::FlushInFlightTasks();
    TestTrue(TEXT("工作线程触发 Token.Cancel() 后在主线程安全完成 Cancelled 终态"), WorkerTokenCancelTask.IsCancelled());

    FCancellationTokenSource NestedChildTokenSource;
    TSharedRef<bool> bParentResumedOnGameThread = MakeShared<bool>(false);
    TGameCoroTask<bool> NestedParentTask = RunNestedChildCancelCoro(
        Owner,
        NestedChildTokenSource.GetToken(),
        bParentResumedOnGameThread
    );
    TestFalse(TEXT("嵌套父协程等待子资产协程挂起中"), NestedParentTask.IsDone());
    TFuture<void> NestedWorkerCancelFuture = Async(EAsyncExecution::ThreadPool, [&NestedChildTokenSource]() {
        NestedChildTokenSource.Cancel();
    });
    NestedWorkerCancelFuture.Wait();
    FGameThreadTaskRunner::FlushInFlightTasks();
    TestTrue(TEXT("工作线程取消子令牌后嵌套父协程正常恢复完成"), NestedParentTask.IsCompleted());
    TestTrue(TEXT("嵌套父协程绝不被劫持到工作线程，严格在 GameThread 恢复"), *bParentResumedOnGameThread);
    if (NestedParentTask.IsCompleted()) {
        TestTrue(TEXT("嵌套父协程正确收到子协程 Cancelled 错误码"), NestedParentTask.GetResult().GetValue());
    }

    TGameCoroTask<UStaticMesh*> WorkerDirectCancelTask = FGameAssetLoader::LoadAsyncCoro<UStaticMesh>(
        TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/__UnitTest_CoroWorkerDirect.__UnitTest_CoroWorkerDirect"))),
        Owner
    );
    TestFalse(TEXT("工作线程直接取消前协程处于挂起态"), WorkerDirectCancelTask.IsDone());
    TFuture<bool> WorkerDirectFuture = Async(EAsyncExecution::ThreadPool, [&WorkerDirectCancelTask]() {
        return WorkerDirectCancelTask.Cancel();
    });
    TestTrue(TEXT("工作线程调用 Task.Cancel() 返回成功"), WorkerDirectFuture.Get());
    TestTrue(TEXT("工作线程调用 Task.Cancel() 后协程状态为 Cancelled"), WorkerDirectCancelTask.IsCancelled());
    FGameThreadTaskRunner::FlushInFlightTasks();

    TSharedRef<FCoroLatentTestState> DiskBatchState = MakeShared<FCoroLatentTestState>();
    DiskBatchState->AliveOwner = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>());
    const FSoftObjectPath PlanePath(TEXT("/Engine/BasicShapes/Plane.Plane"));
    DiskBatchState->BatchTask.Emplace(FGameAssetLoader::LoadBatchAsyncCoro<UStaticMesh>(
        {
            TSoftObjectPtr<UStaticMesh>(PlanePath),
            TSoftObjectPtr<UStaticMesh>(),
            TSoftObjectPtr<UStaticMesh>(PlanePath)
        },
        DiskBatchState->AliveOwner.Get(),
        FCancellationToken::None(),
        EGameAssetPriority::High
    ));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCoroDiskBatchCommand(*this, DiskBatchState));

    TSharedRef<FCoroLatentTestState> DyingState = MakeShared<FCoroLatentTestState>();
    UGameInstance* DyingCoroOwner = NewObject<UGameInstance>();
    DyingState->SingleTask.Emplace(FGameAssetLoader::LoadAsyncCoro<UStaticMesh>(
        TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/__UnitTest_CoroOwnerDie.__UnitTest_CoroOwnerDie"))),
        DyingCoroOwner
    ));
    DyingCoroOwner->MarkAsGarbage();
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForCoroDyingOwnerAbortCommand(*this, DyingState));

    Owner->MarkAsGarbage();
    Resident1->MarkAsGarbage();
    return true;
}

#endif
