/**
 * @file LoadingCoroAwaiterTest.cpp
 * @brief GameFlow::WaitLoadingComplete C++20 协程挂起等待器自动化测试
 */

#include "Async/FCancellationTokenSource.h"
#include "Async/FGameCoroAwaiters.h"
#include "Async/FWaitLoadingCompleteAwaiter.h"
#include "Async/TGameCoroTask.h"
#include "System/FLoadingBarrierToken.h"
#include "System/ULoadingSubsystem.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace {

TGameCoroTask<bool> RunImmediateLoadingCoro(const UObject* ContextObject) {
    const bool bResult = co_await GameFlow::WaitLoadingComplete(ContextObject);
    co_return bResult;
}

TGameCoroTask<bool> RunWaitLoadingCompleteCoro(const UObject* ContextObject, bool& OutFinished) {
    const bool bOk = co_await GameFlow::WaitLoadingComplete(ContextObject);
    if (bOk) {
        OutFinished = true;
    }
    co_return bOk;
}

TGameCoroTask<bool> RunCancellableWaitLoadingCoro(
    const UObject* ContextObject,
    FCancellationToken CancellationToken
) {
    const bool bOk = co_await GameFlow::WaitLoadingComplete(ContextObject, MoveTemp(CancellationToken));
    co_return bOk;
}

TGameCoroTask<void> RunGuardedWaitLoadingCoro(
    const UObject* ContextObject,
    UObject* HostObject,
    bool& OutPostAwaitExecuted
) {
    co_await Game::Coro::BindContext(HostObject);
    co_await GameFlow::WaitLoadingComplete(ContextObject);
    OutPostAwaitExecuted = true;
}

ULoadingSubsystem* CreateTestSubsystemForCoro(UGameInstance*& OutGameInstance) {
    if (GEngine == nullptr) {
        return nullptr;
    }
    OutGameInstance = NewObject<UGameInstance>(GEngine);
    OutGameInstance->InitializeStandalone(FName(TEXT("LoadingCoroAwaiterTestWorld")));
    return OutGameInstance->GetSubsystem<ULoadingSubsystem>();
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingSubsystemCoroAwaiterTest,
    "Game.Flow.System.LoadingSubsystemCoroAwaiter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 GameFlow::WaitLoadingComplete C++20 协程挂起等待原语、非加载态直通、屏障解除唤醒与宿主生命周期自毁熔断
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingSubsystemCoroAwaiterTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    ULoadingSubsystem* LoadingSubsystem = CreateTestSubsystemForCoro(GameInstance);
    TestNotNull(TEXT("加载子系统创建成功"), LoadingSubsystem);
    if (LoadingSubsystem == nullptr || GameInstance == nullptr) {
        return false;
    }

    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    TGameCoroTask<bool> ImmediateTask = RunImmediateLoadingCoro(World);
    TestTrue(TEXT("非加载态下 WaitLoadingComplete 直接同步完成"), ImmediateTask.IsDone());
    TestTrue(TEXT("非加载态下结果状态成功"), ImmediateTask.GetResult().IsSuccess());
    TestTrue(TEXT("非加载态下返回 true"), ImmediateTask.GetResult().GetValue());

    TGameCoroTask<bool> NullContextTask = RunImmediateLoadingCoro(nullptr);
    TestTrue(TEXT("空上下文直接同步完成"), NullContextTask.IsDone());
    TestTrue(TEXT("空上下文结果状态成功"), NullContextTask.GetResult().IsSuccess());
    TestFalse(TEXT("空上下文返回 false"), NullContextTask.GetResult().GetValue());

    FCancellationTokenSource PreCancelledSource;
    PreCancelledSource.Cancel();
    TGameCoroTask<bool> PreCancelledTask = RunCancellableWaitLoadingCoro(World, PreCancelledSource.GetToken());
    TestTrue(TEXT("预取消令牌等待协程直接同步完成"), PreCancelledTask.IsDone());
    TestFalse(TEXT("预取消令牌交付 false"), PreCancelledTask.GetResult().GetValue());

    const FString LevelName = TEXT("/Game/Maps/CoroLevel");
    FCoreUObjectDelegates::PreLoadMap.Broadcast(LevelName);
    TSharedPtr<FLoadingBarrierToken> Barrier = LoadingSubsystem->AcquireLoadingBarrier(FName(TEXT("AsyncBarrier")));

    FCancellationTokenSource InFlightCancelSource;
    TGameCoroTask<bool> InFlightCancelTask = RunCancellableWaitLoadingCoro(World, InFlightCancelSource.GetToken());
    TestFalse(TEXT("挂起中等待协程尚未完成"), InFlightCancelTask.IsDone());
    InFlightCancelSource.Cancel();
    TestTrue(TEXT("取消令牌触发后等待协程立即唤醒完成"), InFlightCancelTask.IsDone());
    TestFalse(TEXT("取消令牌唤醒后返回值交付 false"), InFlightCancelTask.GetResult().GetValue());

    bool bCoroFinished = false;
    TGameCoroTask<bool> WaitTask = RunWaitLoadingCompleteCoro(World, bCoroFinished);
    TestFalse(TEXT("加载与屏障进行中协程处于挂起未完成态"), WaitTask.IsDone());
    TestFalse(TEXT("协程尚未执行结束赋值"), bCoroFinished);

    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World);
    TestFalse(TEXT("屏障未释放前协程依然挂起"), WaitTask.IsDone());

    Barrier->Release();
    TestTrue(TEXT("屏障释放加载完成后协程立即唤醒完成"), WaitTask.IsDone());
    TestTrue(TEXT("协程执行状态正常"), WaitTask.GetResult().IsSuccess());
    TestTrue(TEXT("协程返回值结果为 true"), WaitTask.GetResult().GetValue());
    TestTrue(TEXT("协程成功执行挂起点后逻辑"), bCoroFinished);

    FCoreUObjectDelegates::PreLoadMap.Broadcast(LevelName);
    TSharedPtr<FLoadingBarrierToken> Barrier2 = LoadingSubsystem->AcquireLoadingBarrier(FName(TEXT("Barrier2")));

    bool bGuardedPostAwaitRan = false;
    UGameInstance* TempHost = NewObject<UGameInstance>();
    TGameCoroTask<void> GuardedCoro = RunGuardedWaitLoadingCoro(World, TempHost, bGuardedPostAwaitRan);
    TestFalse(TEXT("守卫协程处于挂起态"), GuardedCoro.IsDone());

    TempHost->MarkAsGarbage();
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World);
    Barrier2->Release();

    TestFalse(TEXT("宿主销毁后挂起点后的代码绝不执行"), bGuardedPostAwaitRan);
    TestTrue(TEXT("宿主销毁后协程处于已取消终态"), GuardedCoro.IsCancelled());

    GameInstance->Shutdown();
    return true;
}

#endif
