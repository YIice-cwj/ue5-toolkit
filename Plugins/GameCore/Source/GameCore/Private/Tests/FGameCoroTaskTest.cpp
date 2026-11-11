/**
 * @file FGameCoroTaskTest.cpp
 * @brief C++20 协程原语集 (TGameCoroTask & FGameCoroAwaiters) 自动化单元测试
 */

#include "Async/FGameCoroAwaiters.h"
#include "Async/FCancellationTokenSource.h"
#include "Async/TGameCoroTask.h"

#if WITH_AUTOMATION_TESTS

#include "Async/TaskGraphInterfaces.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"

#include <atomic>

namespace {

/**
 * @brief 追踪协程帧内局部变量构造与析构次数的 RAII 守卫
 */
struct FCoroFrameRaiiTracker {
    int32* DestructCounter = nullptr;

    explicit FCoroFrameRaiiTracker(int32* InCounter) noexcept
        : DestructCounter(InCounter) {
    }

    ~FCoroFrameRaiiTracker() {
        if (DestructCounter != nullptr) {
            ++(*DestructCounter);
        }
    }

    FCoroFrameRaiiTracker(const FCoroFrameRaiiTracker&) = delete;
    FCoroFrameRaiiTracker& operator=(const FCoroFrameRaiiTracker&) = delete;
};

/**
 * @brief 同步计算的叶子子协程
 * @param BaseValue 输入基数
 * @return 翻倍结果协程
 */
TGameCoroTask<int32> ComputeLeafCoro(int32 BaseValue) {
    co_return BaseValue * 2;
}

/**
 * @brief 嵌套等待子协程的父协程
 * @param Scheduler 节流调度器引用
 * @param InputValue 输入数值
 * @return 累加计算结果协程
 */
TGameCoroTask<int32> ComputeParentCoro(FThrottledTickScheduler& Scheduler, int32 InputValue) {
    const TGameStatusResult<int32> ChildRes = co_await ComputeLeafCoro(InputValue);
    if (ChildRes.IsFailure()) {
        co_return -1;
    }
    co_await Game::Coro::WaitSeconds(Scheduler, 0.1f);
    co_return ChildRes.GetValue() + 10;
}

/**
 * @brief 验证无返回值 void 协程
 * @param Scheduler 节流调度器引用
 * @param OutVisitedStep 输出步进计数
 * @return void 协程任务
 */
TGameCoroTask<void> RunVoidCoro(FThrottledTickScheduler& Scheduler, int32& OutVisitedStep) {
    OutVisitedStep = 1;
    co_await Game::Coro::WaitSeconds(Scheduler, 0.05f);
    OutVisitedStep = 2;
    co_return;
}

/**
 * @brief 绑定 UObject 宿主并在挂起期间测试宿主销毁自毁的协程
 * @param Owner 宿主对象指针
 * @param Scheduler 节流调度器引用
 * @param OutRaiiDestructCount 协程帧局部 RAII 对象析构计数器
 * @param OutPostAwaitExecutedCount 挂起点之后的业务代码执行计数器
 * @return 整型结果协程
 */
TGameCoroTask<int32> RunOwnerGuardedCoro(
    UObject* Owner,
    FThrottledTickScheduler& Scheduler,
    int32& OutRaiiDestructCount,
    int32& OutPostAwaitExecutedCount
) {
    FCoroFrameRaiiTracker LocalRaii(&OutRaiiDestructCount);
    co_await Game::Coro::BindContext(Owner);
    co_await Game::Coro::WaitSeconds(Scheduler, 0.2f);
    ++OutPostAwaitExecutedCount;
    co_return 999;
}

/**
 * @brief 绑定外部协同取消令牌的测试协程
 * @param Token 协同取消令牌
 * @param Scheduler 节流调度器引用
 * @param OutRaiiDestructCount 协程帧局部 RAII 析构计数
 * @param OutPostAwaitExecutedCount 挂起点后业务代码执行计数
 * @return 整型结果协程
 */
TGameCoroTask<int32> RunTokenGuardedCoro(
    FCancellationToken Token,
    FThrottledTickScheduler& Scheduler,
    int32& OutRaiiDestructCount,
    int32& OutPostAwaitExecutedCount
) {
    FCoroFrameRaiiTracker LocalRaii(&OutRaiiDestructCount);
    co_await Game::Coro::BindCancellationToken(MoveTemp(Token));
    co_await Game::Coro::WaitSeconds(Scheduler, 0.2f);
    ++OutPostAwaitExecutedCount;
    co_return 777;
}

/**
 * @brief 验证跨线程切换与后台计算自动回主线程的协程
 * @param bOutRanInBackground 输出是否在后台线程运行过
 * @param bOutResumedOnGameThread 输出是否恢复在 GameThread
 * @return 组合计算结果协程
 */
TGameCoroTask<int32> RunCrossThreadCoro(
    std::atomic<bool>& bOutRanInBackground,
    std::atomic<bool>& bOutResumedOnGameThread
) {
    co_await Game::Coro::SwitchToBackground();
    bOutRanInBackground.store(!IsInGameThread(), std::memory_order_release);

    co_await Game::Coro::SwitchToGameThread();
    bOutResumedOnGameThread.store(IsInGameThread(), std::memory_order_release);

    const int32 BgResult = co_await Game::Coro::RunOnBackground<int32>([]() {
        return 64;
    });
    co_return BgResult + (IsInGameThread() ? 36 : 0);
}

/**
 * @brief 验证条件等待与帧预算时间切片让出的协程
 * @param TickScheduler 节流调度器
 * @param SliceScheduler 时间切片调度器
 * @param bConditionFlag 外部条件引用
 * @param OutSliceProgress 输出切片推进进度
 * @return 完成后的累计值协程
 */
TGameCoroTask<int32> RunWaitUntilAndSliceCoro(
    FThrottledTickScheduler& TickScheduler,
    FTimeSliceScheduler& SliceScheduler,
    bool& bConditionFlag,
    int32& OutSliceProgress
) {
    co_await Game::Coro::WaitUntil(TickScheduler, 0.05f, [&bConditionFlag]() {
        return bConditionFlag;
    });

    for (int32 Step = 0; Step < 3; ++Step) {
        ++OutSliceProgress;
        co_await Game::Coro::YieldTimeSlice(SliceScheduler);
    }

    co_return OutSliceProgress * 10;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCoroBasicAndNestedTest,
    "Game.Core.Async.Coro.BasicAndNested",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证协程基础返回值、void 特化、嵌套子协程 co_await 与完成回调
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameCoroBasicAndNestedTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FThrottledTickScheduler Scheduler;

    TGameCoroTask<int32> ParentTask = ComputeParentCoro(Scheduler, 21);
    TestEqual(TEXT("挂起等待期间状态为 Running"), ParentTask.GetStatus(), EGameCoroStatus::Running);
    TestFalse(TEXT("未完成前 GetResult 返回 InvalidState 失败"), ParentTask.GetResult().IsSuccess());

    int32 CallbackResult = 0;
    ParentTask.WhenCompleted([&CallbackResult](TGameStatusResult<int32> Result) {
        if (Result.IsSuccess()) {
            CallbackResult = Result.GetValue();
        }
    });

    Scheduler.Tick(0.05f);
    TestFalse(TEXT("0.05s 时尚未到达 0.1s 延时"), ParentTask.IsDone());

    Scheduler.Tick(0.06f);
    TestTrue(TEXT("累计 0.11s 后父协程执行完成"), ParentTask.IsCompleted());
    TestEqual(TEXT("嵌套子协程返回值 (21*2)+10 = 52"), ParentTask.GetResult().GetValueOr(-1), 52);
    TestEqual(TEXT("WhenCompleted 回调准确收到结果 52"), CallbackResult, 52);

    int32 VoidStep = 0;
    TGameCoroTask<void> VoidTask = RunVoidCoro(Scheduler, VoidStep);
    TestEqual(TEXT("void 协程 Eager 执行首段至首个挂起点"), VoidStep, 1);
    Scheduler.Tick(0.06f);
    TestTrue(TEXT("void 协程恢复并完成"), VoidTask.IsCompleted());
    TestEqual(TEXT("void 协程完成第二段逻辑"), VoidStep, 2);
    TestTrue(TEXT("void 协程 GetResult 为 Success"), VoidTask.GetResult().IsSuccess());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCoroOwnerDestructionAbortTest,
    "Game.Core.Async.Coro.OwnerDestructionAbortAndRaii",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证挂起期间宿主 UObject 被销毁时挂起点直接拦截并销毁协程帧（零悬垂恢复、局部 RAII 正常析构）
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameCoroOwnerDestructionAbortTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FThrottledTickScheduler Scheduler;
    UGameInstance* TempOwner = NewObject<UGameInstance>();
    TestNotNull(TEXT("成功创建临时宿主 UObject"), TempOwner);

    int32 RaiiDestructCount = 0;
    int32 PostAwaitExecutedCount = 0;

    TGameCoroTask<int32> Task = RunOwnerGuardedCoro(
        TempOwner,
        Scheduler,
        RaiiDestructCount,
        PostAwaitExecutedCount
    );

    TestEqual(TEXT("挂起期间局部 RAII 对象尚未析构"), RaiiDestructCount, 0);
    TestFalse(TEXT("协程处于挂起态"), Task.IsDone());

    TempOwner->MarkAsGarbage();
    Scheduler.Tick(0.25f);

    TestTrue(TEXT("宿主销毁后协程状态自动转为 Cancelled"), Task.IsCancelled());
    TestEqual(TEXT("挂起点之后的悬垂业务代码执行次数严格为 0"), PostAwaitExecutedCount, 0);
    TestEqual(TEXT("协程帧被安全 destroy，局部 RAII 对象精确析构 1 次"), RaiiDestructCount, 1);
    TestTrue(TEXT("GetResult 返回 Cancelled 错误码"), Task.GetResult().IsFailure());
    TestEqual(
        TEXT("错误码为 Game::CommonErrorCodes::Cancelled"),
        Task.GetResult().GetError().ErrorCode,
        Game::CommonErrorCodes::Cancelled
    );

    int32 NullOwnerRaiiCount = 0;
    int32 NullOwnerPostCount = 0;
    TGameCoroTask<int32> NullOwnerTask = RunOwnerGuardedCoro(
        nullptr,
        Scheduler,
        NullOwnerRaiiCount,
        NullOwnerPostCount
    );
    TestTrue(TEXT("传入 nullptr 宿主在 BindContext 处立即拦截并置为 Cancelled"), NullOwnerTask.IsCancelled());
    TestEqual(TEXT("nullptr 宿主后续代码 0 次执行"), NullOwnerPostCount, 0);
    TestEqual(TEXT("nullptr 宿主协程帧立即自毁并析构局部 RAII"), NullOwnerRaiiCount, 1);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCoroCancellationAndSchedulerClearTest,
    "Game.Core.Async.Coro.CancellationAndSchedulerClear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FCancellationToken 取消、Task.Cancel() 以及调度器 ClearAll 清空时的协程帧零泄漏自毁
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameCoroCancellationAndSchedulerClearTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FThrottledTickScheduler Scheduler;
    FCancellationTokenSource Cts;

    int32 TokenRaiiDestructs = 0;
    int32 TokenPostExecs = 0;
    TGameCoroTask<int32> TokenTask = RunTokenGuardedCoro(
        Cts.GetToken(),
        Scheduler,
        TokenRaiiDestructs,
        TokenPostExecs
    );

    Cts.Cancel();
    Scheduler.Tick(0.05f);

    TestTrue(TEXT("取消令牌触发后协程转为 Cancelled"), TokenTask.IsCancelled());
    TestEqual(TEXT("取消后挂起点后代码 0 次执行"), TokenPostExecs, 0);
    TestEqual(TEXT("取消后协程帧安全销毁并触发 RAII 析构"), TokenRaiiDestructs, 1);

    int32 ClearRaiiDestructs = 0;
    int32 ClearPostExecs = 0;
    TGameCoroTask<int32> ClearTask = RunTokenGuardedCoro(
        FCancellationToken::None(),
        Scheduler,
        ClearRaiiDestructs,
        ClearPostExecs
    );
    TestFalse(TEXT("ClearAll 前协程正在挂起"), ClearTask.IsDone());

    Scheduler.ClearAll();
    TestTrue(TEXT("调度器 ClearAll 销毁回调闭包时 FGameCoroSuspensionGuard 自动标记 Cancelled"), ClearTask.IsCancelled());
    TestEqual(TEXT("调度器清空后协程帧自动 destroy 零内存泄漏"), ClearRaiiDestructs, 1);
    TestEqual(TEXT("挂起点后代码 0 次执行"), ClearPostExecs, 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCoroCrossThreadAndSliceTest,
    "Game.Core.Async.Coro.CrossThreadAndTimeSlice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证协程主线程/后台工作线程往返切换、WaitUntil 条件轮询与 YieldTimeSlice 分帧切片推进
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameCoroCrossThreadAndSliceTest::RunTest([[maybe_unused]] const FString& Parameters) {
    std::atomic<bool> bRanInBackground{false};
    std::atomic<bool> bResumedOnGameThread{false};

    TGameCoroTask<int32> ThreadTask = RunCrossThreadCoro(bRanInBackground, bResumedOnGameThread);

    const double TimeoutDeadline = FPlatformTime::Seconds() + 2.0;
    while (!ThreadTask.IsDone() && FPlatformTime::Seconds() < TimeoutDeadline) {
        FPlatformProcess::Yield();
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
    }

    TestTrue(TEXT("跨线程协程顺利执行完成"), ThreadTask.IsCompleted());
    TestTrue(TEXT("SwitchToBackground 确实在后台工作线程恢复"), bRanInBackground.load(std::memory_order_acquire));
    TestTrue(TEXT("SwitchToGameThread 确实切回 GameThread 主线程"), bResumedOnGameThread.load(std::memory_order_acquire));
    TestEqual(TEXT("RunOnBackground 返回值并回主线程汇总为 100"), ThreadTask.GetResult().GetValueOr(0), 100);

    FThrottledTickScheduler TickScheduler;
    FTimeSliceScheduler SliceScheduler;
    double VirtualClock = 0.0;
    SliceScheduler.SetClockSource([&VirtualClock]() {
        VirtualClock += 0.002;
        return VirtualClock;
    });
    SliceScheduler.SetFrameBudgetMilliseconds(1.0);

    bool bReadyFlag = false;
    int32 SliceProgress = 0;
    TGameCoroTask<int32> SliceTask = RunWaitUntilAndSliceCoro(
        TickScheduler,
        SliceScheduler,
        bReadyFlag,
        SliceProgress
    );

    TickScheduler.Tick(0.06f);
    TestEqual(TEXT("条件未满足前 SliceProgress 为 0"), SliceProgress, 0);

    bReadyFlag = true;
    TickScheduler.Tick(0.06f);
    TestEqual(TEXT("条件满足后唤醒并执行第 1 步后挂起在 YieldTimeSlice"), SliceProgress, 1);

    SliceScheduler.Step();
    TestEqual(TEXT("第 1 次帧切片 Step 推进至 2"), SliceProgress, 2);

    SliceScheduler.Step();
    TestEqual(TEXT("第 2 次帧切片 Step 推进至 3"), SliceProgress, 3);

    SliceScheduler.Step();
    TestTrue(TEXT("第 3 次帧切片 Step 后协程全部完成"), SliceTask.IsCompleted());
    TestEqual(TEXT("切片协程最终返回值为 30"), SliceTask.GetResult().GetValueOr(0), 30);

    return true;
}

namespace {

TGameCoroTask<int32> RunNextFrameTestCoro(FThrottledTickScheduler& Scheduler, int32& OutStep) {
    OutStep = 1;
    co_await Game::Coro::NextFrame(Scheduler);
    OutStep = 2;
    co_await Game::Coro::NextFrame(Scheduler);
    OutStep = 3;
    co_return 42;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCoroNextFrameTest,
    "Game.Core.Async.Coro.NextFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 C++20 协程通过 NextFrame 跨帧挂起与下一次心跳精确恢复
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameCoroNextFrameTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FThrottledTickScheduler TickScheduler;
    int32 StepValue = 0;

    TGameCoroTask<int32> CoroTask = RunNextFrameTestCoro(TickScheduler, StepValue);
    TestEqual(TEXT("初始执行至首个 NextFrame 挂起点，Step 为 1"), StepValue, 1);
    TestFalse(TEXT("协程尚未完成"), CoroTask.IsCompleted());

    TickScheduler.Tick(0.016f);
    TestEqual(TEXT("单帧 Tick 后推进至第 2 步并再次挂起"), StepValue, 2);
    TestFalse(TEXT("协程尚未完成"), CoroTask.IsCompleted());

    TickScheduler.Tick(0.016f);
    TestEqual(TEXT("第二次 Tick 后推进至第 3 步"), StepValue, 3);
    TestTrue(TEXT("两次 NextFrame 后协程全部完成"), CoroTask.IsCompleted());
    TestEqual(TEXT("协程最终返回值一致"), CoroTask.GetResult().GetValueOr(0), 42);

    return true;
}

namespace {

TGameCoroTask<int32> RunLongSuspendCoro(
    FThrottledTickScheduler& Scheduler,
    int32& OutRaiiDestructCount,
    int32& OutPostAwaitExecutedCount
) {
    FCoroFrameRaiiTracker LocalRaii(&OutRaiiDestructCount);
    co_await Game::Coro::WaitSeconds(Scheduler, 100.0f);
    ++OutPostAwaitExecutedCount;
    co_return 100;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameCoroImmediateCancelTest,
    "Game.Core.Async.Coro.ImmediateCancelAndCallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Task.Cancel() 立即唤醒挂起守卫自毁协程帧并立即派发 WhenCompleted
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameCoroImmediateCancelTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FThrottledTickScheduler Scheduler;
    int32 RaiiDestructCount = 0;
    int32 PostAwaitExecutedCount = 0;

    TGameCoroTask<int32> Task = RunLongSuspendCoro(
        Scheduler,
        RaiiDestructCount,
        PostAwaitExecutedCount
    );

    TestFalse(TEXT("协程正在挂起 100s"), Task.IsDone());
    TestEqual(TEXT("RAII 尚未析构"), RaiiDestructCount, 0);

    bool bCallbackInvoked = false;
    Task.WhenCompleted([&bCallbackInvoked](TGameStatusResult<int32> Result) {
        if (Result.IsFailure() && Result.GetError().ErrorCode == Game::CommonErrorCodes::Cancelled) {
            bCallbackInvoked = true;
        }
    });

    const bool bCancelled = Task.Cancel();
    TestTrue(TEXT("Cancel() 首次返回 true"), bCancelled);
    TestTrue(TEXT("Cancel() 后协程立即转为 Cancelled"), Task.IsCancelled());
    TestTrue(TEXT("WhenCompleted 回调立即被触发"), bCallbackInvoked);
    TestEqual(TEXT("挂起中的协程帧立即被自毁，RAII 析构 1 次"), RaiiDestructCount, 1);
    TestEqual(TEXT("挂起点后代码 0 次执行"), PostAwaitExecutedCount, 0);

    Scheduler.Tick(200.0f);
    TestEqual(TEXT("后续 Tick 绝不会重复执行业务"), PostAwaitExecutedCount, 0);
    TestEqual(TEXT("RAII 对象绝不重复析构"), RaiiDestructCount, 1);

    return true;
}

#endif // WITH_AUTOMATION_TESTS
