/**
 * @file LoadingCoroAwaiterTest.cpp
 * @brief GameFlow::WaitLoadingComplete C++20 协程等待器全路径与并发安全自动化测试
 */

#include "Async/FCancellationTokenSource.h"
#include "Async/FGameCoroAwaiters.h"
#include "Async/FGameThreadTaskRunner.h"
#include "Async/FWaitLoadingCompleteAwaiter.h"
#include "Async/TGameCoroTask.h"
#include "System/FLoadingBarrierToken.h"
#include "System/ULoadingSubsystem.h"
#include "Tests/GameFlowTestUtils.h"

#if WITH_AUTOMATION_TESTS

#include <atomic>

#include "Async/Async.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"

using Game::Flow::Test::FScopedTestLoadingWorld;

namespace {

/**
 * @brief 基础协程等待用例：直返 WaitLoadingComplete 布尔结果
 * @param ContextObject 世界或实例上下文对象
 * @return 协程任务句柄
 */
TGameCoroTask<bool> RunImmediateLoadingCoro(const UObject* ContextObject) {
    const bool bResult = co_await GameFlow::WaitLoadingComplete(ContextObject);
    co_return bResult;
}

/**
 * @brief 挂起等待完成后置位外部标记的测试协程
 * @param ContextObject 世界或实例上下文对象
 * @param OutFinished 等待成功返回后置为 true
 * @return 协程任务句柄
 */
TGameCoroTask<bool> RunWaitLoadingCompleteCoro(const UObject* ContextObject, bool& OutFinished) {
    const bool bOk = co_await GameFlow::WaitLoadingComplete(ContextObject);
    if (bOk) {
        OutFinished = true;
    }
    co_return bOk;
}

/**
 * @brief 支持取消令牌、超时与恢复线程断言记录的测试协程
 * @param ContextObject 世界或实例上下文对象
 * @param CancellationToken 协同取消令牌
 * @param TimeoutSeconds 超时秒数
 * @param OutResumedOnGameThread 可选输出恢复瞬间是否处于 GameThread
 * @return 协程任务句柄
 */
TGameCoroTask<bool> RunCancellableWaitLoadingCoro(
    const UObject* ContextObject,
    FCancellationToken CancellationToken,
    float TimeoutSeconds,
    bool* OutResumedOnGameThread = nullptr
) {
    const bool bOk = co_await GameFlow::WaitLoadingComplete(ContextObject, MoveTemp(CancellationToken), TimeoutSeconds);
    if (OutResumedOnGameThread != nullptr) {
        *OutResumedOnGameThread = IsInGameThread();
    }
    co_return bOk;
}

/**
 * @brief 绑定宿主 UObject 存活守卫的加载等待测试协程
 * @param ContextObject 世界或实例上下文对象
 * @param HostObject 绑定存活守卫的宿主对象
 * @param OutPostAwaitExecuted 若挂起点之后逻辑被执行则置为 true
 * @return void 协程任务句柄
 */
TGameCoroTask<void> RunGuardedWaitLoadingCoro(
    const UObject* ContextObject,
    UObject* HostObject,
    bool& OutPostAwaitExecuted
) {
    co_await Game::Coro::BindContext(HostObject);
    co_await GameFlow::WaitLoadingComplete(ContextObject);
    OutPostAwaitExecuted = true;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingSubsystemCoroAwaiterTest,
    "Game.Flow.System.LoadingSubsystemCoroAwaiter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证直通、预取消、同/跨线程取消、超时、Task.Cancel 零泄漏、子系统销毁中止、正常唤醒与宿主自毁
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingSubsystemCoroAwaiterTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("LoadingCoroAwaiterTestWorld")));
    TestTrue(TEXT("测试世界初始化成功"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    ULoadingSubsystem* LoadingSubsystem = TestWorld.GetSubsystem();
    UGameInstance* GameInstance = TestWorld.GetGameInstance();
    UWorld* World = TestWorld.GetWorld();

    TGameCoroTask<bool> ImmediateTask = RunImmediateLoadingCoro(World);
    TestTrue(TEXT("非加载态下 WaitLoadingComplete 直接同步完成"), ImmediateTask.IsDone());
    TestTrue(TEXT("非加载态下返回 true"), ImmediateTask.GetResult().GetValue());

    TGameCoroTask<bool> DirectSubsystemTask = RunImmediateLoadingCoro(LoadingSubsystem);
    TestTrue(TEXT("直接传 ULoadingSubsystem* 上下文同步直通返回 true"), DirectSubsystemTask.IsDone() && DirectSubsystemTask.GetResult().GetValue());

    TGameCoroTask<bool> NullContextTask = RunImmediateLoadingCoro(nullptr);
    TestTrue(TEXT("空上下文直接同步完成"), NullContextTask.IsDone());
    TestFalse(TEXT("空上下文返回 false"), NullContextTask.GetResult().GetValue());

    FCancellationTokenSource PreCancelledSource;
    PreCancelledSource.Cancel();
    TGameCoroTask<bool> PreCancelledTask = RunCancellableWaitLoadingCoro(World, PreCancelledSource.GetToken(), 0.0f);
    TestTrue(TEXT("预取消令牌等待协程直接同步完成"), PreCancelledTask.IsDone());
    TestFalse(TEXT("预取消令牌交付 false"), PreCancelledTask.GetResult().GetValue());

    const FString LevelName = TEXT("/Game/Maps/CoroLevel");
    TestTrue(TEXT("进入加载态"), TestWorld.BroadcastPreLoadMap(LevelName));
    FLoadingBarrierToken Barrier = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("AsyncBarrier")));

    FCancellationTokenSource InFlightCancelSource;
    TGameCoroTask<bool> InFlightCancelTask = RunCancellableWaitLoadingCoro(World, InFlightCancelSource.GetToken(), 0.0f);
    TestFalse(TEXT("挂起中等待协程尚未完成"), InFlightCancelTask.IsDone());
    InFlightCancelSource.Cancel();
    TestTrue(TEXT("同线程取消令牌触发后等待协程立即唤醒完成"), InFlightCancelTask.IsDone());
    TestFalse(TEXT("同线程取消唤醒后返回值交付 false"), InFlightCancelTask.GetResult().GetValue());
    TestFalse(TEXT("取消后状态委托已完全解绑零泄漏"), LoadingSubsystem->OnLoadingStateChangedNative.IsBound());
    TestFalse(TEXT("取消后中止委托已完全解绑零泄漏"), LoadingSubsystem->OnLoadingAbortedNative.IsBound());

    FCancellationTokenSource CrossThreadCancelSource;
    bool bCrossThreadResumedOnGameThread = false;
    TGameCoroTask<bool> CrossThreadTask = RunCancellableWaitLoadingCoro(
        World,
        CrossThreadCancelSource.GetToken(),
        5.0f,
        &bCrossThreadResumedOnGameThread
    );
    TestFalse(TEXT("跨线程取消测试协程处于挂起态"), CrossThreadTask.IsDone());

    std::atomic<bool> bWorkerCancelledOffGameThread{false};
    TFuture<void> WorkerCancelFuture = Async(
        EAsyncExecution::ThreadPool,
        [&CrossThreadCancelSource, &bWorkerCancelledOffGameThread]() {
            bWorkerCancelledOffGameThread.store(!IsInGameThread(), std::memory_order_release);
            CrossThreadCancelSource.Cancel();
        }
    );
    WorkerCancelFuture.Wait();
    TestTrue(TEXT("排空后台任务并泵送主线程续体队列成功"), FGameThreadTaskRunner::FlushInFlightTasks(2.0));
    TestTrue(TEXT("取消请求确由非主线程工作线程发起"), bWorkerCancelledOffGameThread.load(std::memory_order_acquire));
    TestTrue(TEXT("跨线程取消后协程已在主线程完成恢复"), CrossThreadTask.IsDone());
    TestTrue(TEXT("跨线程取消后的协程恢复严格落在 GameThread"), bCrossThreadResumedOnGameThread);
    TestTrue(TEXT("跨线程取消结果状态有效"), CrossThreadTask.GetResult().IsSuccess());
    if (CrossThreadTask.GetResult().IsSuccess()) {
        TestFalse(TEXT("跨线程取消交付 false"), CrossThreadTask.GetResult().GetValue());
    }
    TestFalse(TEXT("跨线程取消后状态委托零泄漏"), LoadingSubsystem->OnLoadingStateChangedNative.IsBound());
    TestFalse(TEXT("跨线程取消后中止委托零泄漏"), LoadingSubsystem->OnLoadingAbortedNative.IsBound());

    TGameCoroTask<bool> TimeoutTask = RunCancellableWaitLoadingCoro(World, FCancellationToken::None(), 0.05f);
    TestFalse(TEXT("超时协程初始挂起"), TimeoutTask.IsDone());
    ++GFrameCounter;
    GameInstance->GetTimerManager().Tick(0.01f);
    ++GFrameCounter;
    GameInstance->GetTimerManager().Tick(0.1f);
    TestTrue(TEXT("定时器推进超过阈值后协程立即唤醒完成"), TimeoutTask.IsDone());
    TestTrue(TEXT("超时唤醒后结果有效"), TimeoutTask.GetResult().IsSuccess());
    if (TimeoutTask.GetResult().IsSuccess()) {
        TestFalse(TEXT("超时唤醒后返回值交付 false"), TimeoutTask.GetResult().GetValue());
    }
    TestFalse(TEXT("超时唤醒后状态委托零泄漏"), LoadingSubsystem->OnLoadingStateChangedNative.IsBound());
    TestFalse(TEXT("超时唤醒后中止委托零泄漏"), LoadingSubsystem->OnLoadingAbortedNative.IsBound());

    FCancellationTokenSource TaskCancelTokenSource;
    TGameCoroTask<bool> TaskCancelTarget = RunCancellableWaitLoadingCoro(World, TaskCancelTokenSource.GetToken(), 10.0f);
    TestFalse(TEXT("Task.Cancel 目标协程处于挂起态"), TaskCancelTarget.IsDone());
    TestTrue(TEXT("挂起期已绑定子系统状态委托"), LoadingSubsystem->OnLoadingStateChangedNative.IsBound());
    TestTrue(TEXT("主动调用 Task.Cancel() 成功"), TaskCancelTarget.Cancel());
    TestTrue(TEXT("Task.Cancel() 后协程立即进入 Cancelled 终态"), TaskCancelTarget.IsCancelled());
    TestFalse(TEXT("Task.Cancel() 后子系统状态委托立即解绑零泄漏"), LoadingSubsystem->OnLoadingStateChangedNative.IsBound());
    TestFalse(TEXT("Task.Cancel() 后子系统中止委托立即解绑零泄漏"), LoadingSubsystem->OnLoadingAbortedNative.IsBound());

    TGameCoroTask<bool> WorkerTaskCancelTarget = RunCancellableWaitLoadingCoro(World, FCancellationToken::None(), 10.0f);
    TestFalse(TEXT("跨线程 Task.Cancel 目标协程处于挂起态"), WorkerTaskCancelTarget.IsDone());
    TFuture<bool> WorkerTaskCancelFuture = Async(EAsyncExecution::ThreadPool, [&WorkerTaskCancelTarget]() {
        return WorkerTaskCancelTarget.Cancel();
    });
    TestTrue(TEXT("工作线程调用 Task.Cancel() 成功"), WorkerTaskCancelFuture.Get());
    TestTrue(TEXT("工作线程调用 Task.Cancel() 后进入 Cancelled 终态"), WorkerTaskCancelTarget.IsCancelled());
    TestTrue(TEXT("泵送主线程清理回调完成"), FGameThreadTaskRunner::FlushInFlightTasks(2.0));
    TestFalse(TEXT("跨线程 Task.Cancel() 后状态委托零泄漏"), LoadingSubsystem->OnLoadingStateChangedNative.IsBound());
    TestFalse(TEXT("跨线程 Task.Cancel() 后中止委托零泄漏"), LoadingSubsystem->OnLoadingAbortedNative.IsBound());

    bool bCoroFinished = false;
    TGameCoroTask<bool> WaitTask = RunWaitLoadingCompleteCoro(World, bCoroFinished);
    TestFalse(TEXT("加载与屏障进行中协程处于挂起未完成态"), WaitTask.IsDone());
    TestFalse(TEXT("协程尚未执行结束赋值"), bCoroFinished);

    TestWorld.BroadcastPostLoadMap();
    TestFalse(TEXT("屏障未释放前协程依然挂起"), WaitTask.IsDone());

    Barrier.Release();
    TestTrue(TEXT("屏障释放加载完成后协程立即唤醒完成"), WaitTask.IsDone());
    TestTrue(TEXT("协程执行状态正常"), WaitTask.GetResult().IsSuccess());
    TestTrue(TEXT("协程返回值结果为 true"), WaitTask.GetResult().GetValue());
    TestTrue(TEXT("协程成功执行挂起点后逻辑"), bCoroFinished);

    TestTrue(TEXT("进入第二轮加载以验证宿主销毁自毁熔断"), TestWorld.BroadcastPreLoadMap(LevelName));
    FLoadingBarrierToken Barrier2 = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("Barrier2")));

    bool bGuardedPostAwaitRan = false;
    UGameInstance* TempHost = NewObject<UGameInstance>();
    TGameCoroTask<void> GuardedCoro = RunGuardedWaitLoadingCoro(World, TempHost, bGuardedPostAwaitRan);
    TestFalse(TEXT("守卫协程处于挂起态"), GuardedCoro.IsDone());

    TempHost->MarkAsGarbage();
    TestWorld.BroadcastPostLoadMap();
    Barrier2.Release();

    TestFalse(TEXT("宿主销毁后挂起点后的代码绝不执行"), bGuardedPostAwaitRan);
    TestTrue(TEXT("宿主销毁后协程处于已取消终态"), GuardedCoro.IsCancelled());

    TestTrue(TEXT("进入第三轮加载以验证子系统销毁中止返回 false"), TestWorld.BroadcastPreLoadMap(TEXT("/Game/Maps/DeinitCoroLevel")));
    TGameCoroTask<bool> DeinitWaitTask = RunImmediateLoadingCoro(World);
    TestFalse(TEXT("销毁前协程处于挂起态"), DeinitWaitTask.IsDone());

    TestWorld.ShutdownNow();
    TestTrue(TEXT("子系统 Deinitialize 立即唤醒挂起协程"), DeinitWaitTask.IsDone());
    TestTrue(TEXT("协程正常收尾无悬挂"), DeinitWaitTask.GetResult().IsSuccess());
    TestFalse(TEXT("子系统销毁中止必须交付 false 而非误报 true"), DeinitWaitTask.GetResult().GetValue());

    return true;
}

#endif
