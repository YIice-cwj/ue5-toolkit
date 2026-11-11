/**
 * @file TGameCoroTask.h
 * @brief C++20 无栈协程核心任务类型、共享状态机与挂起点生命周期自毁守卫
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FCancellationToken.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "Types/TGameResult.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include <atomic>
#include <coroutine>
#include <type_traits>

class UObject;
class FGameCoroSuspensionGuard;

/**
 * @brief 协程运行生命周期状态枚举
 */
enum class EGameCoroStatus : uint8 {
    Running = 0,
    Completed = 1,
    Cancelled = 2
};

/**
 * @brief 协程内宿主弱引用与协同取消令牌绑定描述符
 */
struct GAMECORE_API FGameCoroContextBinding {
    TWeakObjectPtr<UObject> Owner;
    FCancellationToken Token;
    bool bHasOwner = false;
};

/**
 * @brief 协程跨线程引用计数共享状态基类（解耦外部句柄与底层 coroutine_handle 生命周期）
 */
class GAMECORE_API FGameCoroStateBase : public TSharedFromThis<FGameCoroStateBase, ESPMode::ThreadSafe> {
public:
    using FCompletionCallback = TFunction<void(EGameCoroStatus)>;

public:
    FGameCoroStateBase() = default;
    virtual ~FGameCoroStateBase();

    FGameCoroStateBase(const FGameCoroStateBase&) = delete;
    FGameCoroStateBase& operator=(const FGameCoroStateBase&) = delete;

    /**
     * @brief 绑定宿主 UObject 生命周期守卫与外部取消令牌
     * @param InOwner 宿主对象弱引用
     * @param bInHasOwner 是否启用宿主存活强制校验
     * @param InToken 外部协同取消令牌
     */
    void BindExecutionContext(
        TWeakObjectPtr<UObject> InOwner,
        bool bInHasOwner,
        FCancellationToken InToken
    ) noexcept {
        FScopeLock Lock(&StateLock);
        OwnerWeak = InOwner;
        bOwnerNullAtBind = bInHasOwner && InOwner.IsExplicitlyNull();
        bHasOwnerGuard.store(bInHasOwner, std::memory_order_release);
        ExternalToken = MoveTemp(InToken);
    }

    /**
     * @brief 主动请求取消当前协程并立即唤醒挂起守卫自毁协程帧
     * @return 首次触发取消请求返回 true
     */
    bool RequestCancel() noexcept;

    /**
     * @brief 注册当前挂起状态的守卫弱引用（支持外部主动 Cancel 时立即唤醒并自毁）
     * @param InGuard 挂起守卫共享引用
     */
    void RegisterSuspensionGuard(const TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe>& InGuard);

    /**
     * @brief 注销当前挂起守卫
     * @param InGuard 待注销守卫指针
     */
    void UnregisterSuspensionGuard(const FGameCoroSuspensionGuard* InGuard);

    /**
     * @brief 查询当前协程是否已被请求取消或绑定宿主已销毁失效
     * @return 应当立即中止并销毁协程帧返回 true
     */
    [[nodiscard]] bool ShouldAbort() const noexcept {
        if (bCancelRequested.load(std::memory_order_acquire)) {
            return true;
        }
        if (Status.load(std::memory_order_acquire) == EGameCoroStatus::Cancelled) {
            return true;
        }
        if (ExternalToken.IsCancellationRequested()) {
            return true;
        }
        if (bHasOwnerGuard.load(std::memory_order_acquire)) {
            TWeakObjectPtr<UObject> LocalOwner;
            bool bLocalNullAtBind = false;
            {
                FScopeLock Lock(&StateLock);
                LocalOwner = OwnerWeak;
                bLocalNullAtBind = bOwnerNullAtBind;
            }
            if (bLocalNullAtBind) {
                return true;
            }
            if (IsInGameThread()) {
                if (!LocalOwner.IsValid()) {
                    return true;
                }
            } else {
                if (!LocalOwner.IsValid(false, true)) {
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * @brief 获取当前协程的宿主弱引用
     * @return 宿主弱引用
     */
    [[nodiscard]] TWeakObjectPtr<UObject> GetOwnerWeak() const noexcept {
        FScopeLock Lock(&StateLock);
        return OwnerWeak;
    }

    /**
     * @brief 查询是否启用了宿主 UObject 生命周期守卫
     * @return 已启用返回 true
     */
    [[nodiscard]] bool HasOwnerGuard() const noexcept {
        return bHasOwnerGuard.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取当前协程的生命周期状态
     * @return 状态枚举值
     */
    [[nodiscard]] EGameCoroStatus GetStatus() const noexcept {
        return Status.load(std::memory_order_acquire);
    }

    /**
     * @brief 将协程状态原子标记为已取消并出锁派发所有完成回调
     * @return 首次进入终态返回 true
     */
    bool MarkCancelled() {
        TArray<FCompletionCallback> LocalCallbacks;
        {
            FScopeLock Lock(&StateLock);
            EGameCoroStatus Expected = EGameCoroStatus::Running;
            if (!Status.compare_exchange_strong(Expected, EGameCoroStatus::Cancelled, std::memory_order_acq_rel)) {
                return false;
            }
            bCancelRequested.store(true, std::memory_order_release);
            LocalCallbacks = MoveTemp(CompletionCallbacks);
        }
        for (const FCompletionCallback& Callback : LocalCallbacks) {
            if (Callback) {
                Callback(EGameCoroStatus::Cancelled);
            }
        }
        return true;
    }

    /**
     * @brief 注册当协程到达终态（Completed 或 Cancelled）时执行的回调（若已处于终态则出锁立即执行）
     * @param InCallback 终态回调闭包
     */
    void AddCompletionCallback(FCompletionCallback InCallback) {
        if (!InCallback) {
            return;
        }
        EGameCoroStatus ImmediateStatus = EGameCoroStatus::Running;
        bool bInvokeImmediately = false;
        {
            FScopeLock Lock(&StateLock);
            ImmediateStatus = Status.load(std::memory_order_acquire);
            if (ImmediateStatus != EGameCoroStatus::Running) {
                bInvokeImmediately = true;
            } else {
                CompletionCallbacks.Add(MoveTemp(InCallback));
            }
        }
        if (bInvokeImmediately) {
            InCallback(ImmediateStatus);
        }
    }

protected:
    /**
     * @brief 将协程状态原子标记为已完成并出锁派发所有完成回调
     * @return 首次进入终态返回 true
     */
    bool MarkCompletedProtected() {
        TArray<FCompletionCallback> LocalCallbacks;
        {
            FScopeLock Lock(&StateLock);
            EGameCoroStatus Expected = EGameCoroStatus::Running;
            if (!Status.compare_exchange_strong(Expected, EGameCoroStatus::Completed, std::memory_order_acq_rel)) {
                return false;
            }
            LocalCallbacks = MoveTemp(CompletionCallbacks);
        }
        for (const FCompletionCallback& Callback : LocalCallbacks) {
            if (Callback) {
                Callback(EGameCoroStatus::Completed);
            }
        }
        return true;
    }

protected:
    mutable FCriticalSection StateLock;
    TWeakObjectPtr<UObject> OwnerWeak;
    FCancellationToken ExternalToken;
    TArray<FCompletionCallback> CompletionCallbacks;
    std::atomic<EGameCoroStatus> Status{EGameCoroStatus::Running};
    std::atomic<bool> bCancelRequested{false};
    std::atomic<bool> bHasOwnerGuard{false};
    TWeakPtr<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> CurrentSuspensionGuard;
    bool bOwnerNullAtBind = false;
};

/**
 * @brief 泛型协程共享状态（承载返回值或取消错误）
 * @tparam T 协程返回值类型
 */
template <typename T>
class TGameCoroState final : public FGameCoroStateBase {
public:
    TGameCoroState() = default;

    /**
     * @brief 写入协程返回值并触发已完成终态通知
     * @tparam U 可转换为 T 的值类型
     * @param InValue 返回值
     */
    template <typename U>
    void SetResult(U&& InValue) {
        {
            FScopeLock Lock(&StateLock);
            if (Status.load(std::memory_order_acquire) != EGameCoroStatus::Running) {
                return;
            }
            ResultStorage.Emplace(Forward<U>(InValue));
        }
        MarkCompletedProtected();
    }

    /**
     * @brief 提取当前协程的执行结果（已完成返回 Success，已取消返回 Cancelled 错误）
     * @return 包含返回值或错误信息的 TGameStatusResult<T>
     */
    [[nodiscard]] TGameStatusResult<T> GetResult() const {
        FScopeLock Lock(&StateLock);
        const EGameCoroStatus CurrentStatus = Status.load(std::memory_order_acquire);
        if (CurrentStatus == EGameCoroStatus::Completed && ResultStorage.IsSet()) {
            return TGameStatusResult<T>::Success(ResultStorage.GetValue());
        }
        if (CurrentStatus == EGameCoroStatus::Cancelled || bCancelRequested.load(std::memory_order_acquire)) {
            return TGameStatusResult<T>::Failure(
                Game::CommonErrorCodes::Cancelled,
                TEXT("Coroutine was cancelled or bound UObject owner expired.")
            );
        }
        return TGameStatusResult<T>::Failure(
            Game::CommonErrorCodes::InvalidState,
            TEXT("Coroutine has not completed yet.")
        );
    }

private:
    TOptional<T> ResultStorage;
};

/**
 * @brief void 特化协程共享状态
 */
template <>
class TGameCoroState<void> final : public FGameCoroStateBase {
public:
    TGameCoroState() = default;

    /**
     * @brief 标记 void 协程正常执行完成并派发通知
     */
    void SetCompleted() {
        MarkCompletedProtected();
    }

    /**
     * @brief 提取 void 协程的执行状态结果
     * @return 成功完成返回 Success()，已取消返回 Cancelled 错误
     */
    [[nodiscard]] TGameStatusResult<void> GetResult() const {
        const EGameCoroStatus CurrentStatus = GetStatus();
        if (CurrentStatus == EGameCoroStatus::Completed) {
            return TGameStatusResult<void>::Success();
        }
        if (CurrentStatus == EGameCoroStatus::Cancelled || ShouldAbort()) {
            return TGameStatusResult<void>::Failure(
                Game::CommonErrorCodes::Cancelled,
                TEXT("Coroutine was cancelled or bound UObject owner expired.")
            );
        }
        return TGameStatusResult<void>::Failure(
            Game::CommonErrorCodes::InvalidState,
            TEXT("Coroutine has not completed yet.")
        );
    }
};

/**
 * @brief 挂起点协程帧 RAII 托管守卫（确保 resume 与 destroy 严格互斥且仅执行一次，调度器清退时自动销毁帧防泄漏）
 */
class GAMECORE_API FGameCoroSuspensionGuard : public TSharedFromThis<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> {
public:
    /**
     * @brief 构造绑定挂起协程帧与共享状态的 RAII 守卫
     * @param InHandle 挂起中的标准协程句柄
     * @param InState 协程共享状态引用
     */
    FGameCoroSuspensionGuard(
        std::coroutine_handle<> InHandle,
        TSharedRef<FGameCoroStateBase, ESPMode::ThreadSafe> InState
    ) noexcept
        : CoroHandle(InHandle)
        , State(MoveTemp(InState)) {
    }

    /**
     * @brief 析构函数：若挂起回调因调度器清空或关停从未被调用，自动标记 Cancelled 并销毁协程帧防内存泄漏
     */
    ~FGameCoroSuspensionGuard() {
        AbortAndDestroyFrame();
    }

    FGameCoroSuspensionGuard(const FGameCoroSuspensionGuard&) = delete;
    FGameCoroSuspensionGuard& operator=(const FGameCoroSuspensionGuard&) = delete;

    /**
     * @brief 唤醒前校验宿主存活与取消状态：若已失效则原地 destroy 协程帧，否则安全 resume 协程
     * @return 成功恢复执行返回 true，被拦截并自毁返回 false
     */
    bool ResumeOrAbort() {
        if (!bSuspendCompleted.load(std::memory_order_acquire)) {
            bPendingResume.store(true, std::memory_order_release);
            return true;
        }
        return ResumeOrAbortInternal();
    }

    /**
     * @brief 标记 await_suspend 已安全执行完成并退出底层调用栈，若期间有待恢复请求则统一派发恢复
     */
    void NotifySuspendCompleted() {
        bSuspendCompleted.store(true, std::memory_order_release);
        if (bPendingResume.load(std::memory_order_acquire)) {
            ResumeOrAbortInternal();
        }
    }

    /**
     * @brief 强制中止并原地销毁挂起中的协程帧
     */
    void AbortAndDestroyFrame() {
        if (bHandled.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        State->UnregisterSuspensionGuard(this);
        std::coroutine_handle<> LocalHandle = CoroHandle;
        CoroHandle = nullptr;
        State->MarkCancelled();
        if (LocalHandle) {
            LocalHandle.destroy();
        }
    }

    /**
     * @brief 获取关联的协程共享状态引用
     * @return 共享状态引用
     */
    [[nodiscard]] const TSharedRef<FGameCoroStateBase, ESPMode::ThreadSafe>& GetState() const noexcept {
        return State;
    }

private:
    /**
     * @brief 执行实际恢复或自毁
     * @return 成功恢复返回 true，取消或自毁返回 false
     */
    bool ResumeOrAbortInternal() {
        if (bHandled.exchange(true, std::memory_order_acq_rel)) {
            return false;
        }
        State->UnregisterSuspensionGuard(this);
        std::coroutine_handle<> LocalHandle = CoroHandle;
        CoroHandle = nullptr;
        if (!LocalHandle) {
            return false;
        }
        if (State->ShouldAbort()) {
            State->MarkCancelled();
            LocalHandle.destroy();
            return false;
        }
        LocalHandle.resume();
        return true;
    }

private:
    std::coroutine_handle<> CoroHandle;
    TSharedRef<FGameCoroStateBase, ESPMode::ThreadSafe> State;
    std::atomic<bool> bHandled{false};
    std::atomic<bool> bSuspendCompleted{false};
    std::atomic<bool> bPendingResume{false};
};

inline FGameCoroStateBase::~FGameCoroStateBase() = default;

inline bool FGameCoroStateBase::RequestCancel() noexcept {
    const bool bFirst = !bCancelRequested.exchange(true, std::memory_order_acq_rel);
    if (bFirst) {
        TSharedPtr<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> GuardPin;
        {
            FScopeLock Lock(&StateLock);
            GuardPin = CurrentSuspensionGuard.Pin();
            CurrentSuspensionGuard.Reset();
        }
        if (GuardPin.IsValid()) {
            GuardPin->AbortAndDestroyFrame();
        } else {
            MarkCancelled();
        }
    }
    return bFirst;
}

inline void FGameCoroStateBase::RegisterSuspensionGuard(
    const TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe>& InGuard
) {
    FScopeLock Lock(&StateLock);
    CurrentSuspensionGuard = InGuard;
    if (bCancelRequested.load(std::memory_order_acquire) || ExternalToken.IsCancellationRequested()) {
        Lock.Unlock();
        InGuard->AbortAndDestroyFrame();
    }
}

inline void FGameCoroStateBase::UnregisterSuspensionGuard(const FGameCoroSuspensionGuard* InGuard) {
    FScopeLock Lock(&StateLock);
    if (TSharedPtr<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> Pinned = CurrentSuspensionGuard.Pin()) {
        if (Pinned.Get() == InGuard) {
            CurrentSuspensionGuard.Reset();
        }
    }
}

namespace Game::Coro {
using FGameCoroSuspensionGuard = ::FGameCoroSuspensionGuard;
}

namespace Game::Coro::Detail {

/**
 * @brief 协程内绑定宿主与取消令牌的内部 Awaiter
 */
struct FBindContextAwaiter {
    TSharedRef<FGameCoroStateBase, ESPMode::ThreadSafe> State;
    FGameCoroContextBinding Binding;

    /**
     * @brief 写入绑定配置并立即检查是否已处于失效/取消态
     * @return 宿主有效且未取消返回 true（零开销直通），否则返回 false 进入挂起自毁
     */
    [[nodiscard]] bool await_ready() const noexcept {
        State->BindExecutionContext(Binding.Owner, Binding.bHasOwner, Binding.Token);
        return !State->ShouldAbort();
    }

    /**
     * @brief 若绑定时即已失效，原地标记 Cancelled 并销毁协程帧
     * @param InHandle 当前协程句柄
     * @return 转移给空协程安全交接控制权
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> InHandle) const noexcept {
        State->MarkCancelled();
        InHandle.destroy();
        return std::noop_coroutine();
    }

    /**
     * @brief 恢复空操作
     */
    constexpr void await_resume() const noexcept {
    }
};

/**
 * @brief 包装任意底层 Awaiter 的生命周期守卫代理（在挂起入口与恢复前强制拦截取消/宿主销毁）
 * @tparam TInnerAwaiter 被包装的底层 Awaiter 类型
 */
template <typename TInnerAwaiter>
class TGuardedAwaiter {
public:
    /**
     * @brief 构造生命周期守卫代理 Awaiter
     * @tparam TActualAwaiter 底层等待器通用转发类型
     * @param InState 当前协程共享状态
     * @param InAwaiter 底层 Awaiter 实例
     */
    template <typename TActualAwaiter>
    TGuardedAwaiter(
        TSharedRef<FGameCoroStateBase, ESPMode::ThreadSafe> InState,
        TActualAwaiter&& InAwaiter
    )
        : State(MoveTemp(InState))
        , InnerAwaiter(Forward<TActualAwaiter>(InAwaiter)) {
    }

    /**
     * @brief 若当前协程已失效则强制进入 await_suspend 自毁；否则委托给底层 Awaiter
     * @return 无需挂起返回 true
     */
    [[nodiscard]] bool await_ready() {
        if (State->ShouldAbort()) {
            return false;
        }
        return InnerAwaiter.await_ready();
    }

    /**
     * @brief 挂起当前协程：若已失效则原地自毁，否则创建 FGameCoroSuspensionGuard 交由底层 Awaiter 调度
     * @param InHandle 当前协程句柄
     */
    void await_suspend(std::coroutine_handle<> InHandle) {
        TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> Guard =
            MakeShared<FGameCoroSuspensionGuard, ESPMode::ThreadSafe>(InHandle, State);
        if (State->ShouldAbort()) {
            Guard->AbortAndDestroyFrame();
            return;
        }
        State->RegisterSuspensionGuard(Guard);
        InnerAwaiter.SuspendWithGuard(Guard);
        Guard->NotifySuspendCompleted();
    }

    /**
     * @brief 恢复执行并提取底层 Awaiter 返回值
     * @return 底层 Awaiter 结果
     */
    decltype(auto) await_resume() {
        return InnerAwaiter.await_resume();
    }

private:
    TSharedRef<FGameCoroStateBase, ESPMode::ThreadSafe> State;
    TInnerAwaiter InnerAwaiter;
};

} // namespace Game::Coro::Detail

template <typename T = void>
class TGameCoroTask;

namespace Game::Coro::Detail {

/**
 * @brief 子协程嵌套 co_await 等待器
 * @tparam TChild 子协程返回类型
 */
template <typename TChild>
class TChildTaskAwaiter {
public:
    /**
     * @brief 构造子协程等待器
     * @param InChildState 子协程共享状态
     */
    explicit TChildTaskAwaiter(TSharedRef<TGameCoroState<TChild>, ESPMode::ThreadSafe> InChildState) noexcept
        : ChildState(MoveTemp(InChildState)) {
    }

    /**
     * @brief 若子协程已处于终态则无需挂起
     * @return 已完成或已取消返回 true
     */
    [[nodiscard]] bool await_ready() const noexcept {
        return ChildState->GetStatus() != EGameCoroStatus::Running;
    }

    /**
     * @brief 将父协程的恢复守卫挂载至子协程完成回调链
     * @param InParentGuard 父协程挂起守卫
     */
    void SuspendWithGuard(TSharedRef<FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InParentGuard) {
        ChildState->AddCompletionCallback([InParentGuard]([[maybe_unused]] EGameCoroStatus ChildFinalStatus) {
            InParentGuard->ResumeOrAbort();
        });
    }

    /**
     * @brief 子协程结束恢复父协程后返回 TGameStatusResult<TChild>
     * @return 子协程执行结果
     */
    [[nodiscard]] TGameStatusResult<TChild> await_resume() const {
        return ChildState->GetResult();
    }

private:
    TSharedRef<TGameCoroState<TChild>, ESPMode::ThreadSafe> ChildState;
};

/**
 * @brief 判定类型是否为 TGameCoroTask 实例的特征模板
 * @tparam T Candidate 类型
 */
template <typename T>
struct TIsGameCoroTask : std::false_type {};

template <typename U>
struct TIsGameCoroTask<TGameCoroTask<U>> : std::true_type {};

/**
 * @brief 协程 Promise 公共基类（负责初末态挂起策略、异常静默兜底与 await_transform 透明包装）
 * @tparam T 协程返回值类型
 */
template <typename T>
class TGameCoroPromiseBase {
public:
    /**
     * @brief 默认构造：分配跨线程共享状态
     */
    TGameCoroPromiseBase()
        : State(MakeShared<TGameCoroState<T>, ESPMode::ThreadSafe>()) {
    }

    /**
     * @brief 初始挂起策略：Eager 启动直达首个挂起点
     * @return std::suspend_never
     */
    [[nodiscard]] constexpr std::suspend_never initial_suspend() const noexcept {
        return {};
    }

    /**
     * @brief 最终挂起策略：结果已写入独立共享状态并派发完毕，返回 suspend_never 由运行时自动释放协程帧
     * @return std::suspend_never
     */
    [[nodiscard]] constexpr std::suspend_never final_suspend() const noexcept {
        return {};
    }

    /**
     * @brief UE5 无异常编译环境兜底：若触发异常则将状态置为已取消
     */
    void unhandled_exception() noexcept {
        State->MarkCancelled();
    }

    /**
     * @brief 拦截上下文绑定指令 co_await Game::Coro::BindContext(...)
     * @param InBinding 宿主与取消令牌绑定描述符
     * @return 绑定 Awaiter
     */
    [[nodiscard]] FBindContextAwaiter await_transform(FGameCoroContextBinding InBinding) noexcept {
        return FBindContextAwaiter{State, MoveTemp(InBinding)};
    }

    /**
     * @brief 拦截子协程嵌套等待 co_await TGameCoroTask<U>（左值或右值）
     * @tparam U 子协程返回值类型
     * @param InChildTask 被等待的子协程句柄
     * @return 注入父协程生命周期守卫的子任务 Awaiter
     */
    template <typename U>
    [[nodiscard]] TGuardedAwaiter<TChildTaskAwaiter<U>> await_transform(const TGameCoroTask<U>& InChildTask) {
        return TGuardedAwaiter<TChildTaskAwaiter<U>>(
            State,
            TChildTaskAwaiter<U>(InChildTask.GetSharedState())
        );
    }

    /**
     * @brief 拦截右值子协程嵌套等待 co_await TGameCoroTask<U>
     * @tparam U 子协程返回值类型
     * @param InChildTask 被等待的子协程右值句柄
     * @return 注入父协程生命周期守卫的子任务 Awaiter
     */
    template <typename U>
    [[nodiscard]] TGuardedAwaiter<TChildTaskAwaiter<U>> await_transform(TGameCoroTask<U>&& InChildTask) {
        return TGuardedAwaiter<TChildTaskAwaiter<U>>(
            State,
            TChildTaskAwaiter<U>(InChildTask.GetSharedState())
        );
    }

    /**
     * @brief 拦截所有实现了 SuspendWithGuard 协议的 GameCore Awaiter 并自动注入生命周期自毁守卫
     * @tparam TAwaiter 满足 SuspendWithGuard 协议的等待器类型
     * @param InAwaiter 原始等待器右值
     * @return 包装后的安全等待器
     */
    template <
        typename TAwaiter,
        typename = std::enable_if_t<!TIsGameCoroTask<std::decay_t<TAwaiter>>::value>
    >
    [[nodiscard]] TGuardedAwaiter<std::decay_t<TAwaiter>> await_transform(TAwaiter&& InAwaiter) {
        using FDecayedAwaiter = std::decay_t<TAwaiter>;
        return TGuardedAwaiter<FDecayedAwaiter>(State, Forward<TAwaiter>(InAwaiter));
    }

    /**
     * @brief 获取当前协程关联的共享状态
     * @return 共享状态引用
     */
    [[nodiscard]] const TSharedRef<TGameCoroState<T>, ESPMode::ThreadSafe>& GetState() const noexcept {
        return State;
    }

protected:
    TSharedRef<TGameCoroState<T>, ESPMode::ThreadSafe> State;
};

/**
 * @brief 泛型值返回协程 Promise 实现
 * @tparam T 返回值类型
 */
template <typename T>
class TGameCoroPromise final : public TGameCoroPromiseBase<T> {
public:
    /**
     * @brief 创建关联的外部协程任务句柄
     * @return TGameCoroTask<T> 实例
     */
    [[nodiscard]] TGameCoroTask<T> get_return_object() noexcept;

    /**
     * @brief 接收 co_return 返回值并写入共享状态（若此时宿主已失效则转为标记 Cancelled）
     * @tparam U 可转换为 T 的返回表达式类型
     * @param InValue 返回值
     */
    template <typename U>
    void return_value(U&& InValue) {
        if (this->State->ShouldAbort()) {
            this->State->MarkCancelled();
            return;
        }
        this->State->SetResult(Forward<U>(InValue));
    }
};

/**
 * @brief void 返回协程 Promise 特化实现
 */
template <>
class TGameCoroPromise<void> final : public TGameCoroPromiseBase<void> {
public:
    /**
     * @brief 创建关联的外部 void 协程任务句柄
     * @return TGameCoroTask<void> 实例
     */
    [[nodiscard]] TGameCoroTask<void> get_return_object() noexcept;

    /**
     * @brief 处理 co_return 完成并通知共享状态（若此时宿主已失效则转为标记 Cancelled）
     */
    void return_void() {
        if (this->State->ShouldAbort()) {
            this->State->MarkCancelled();
            return;
        }
        this->State->SetCompleted();
    }
};

} // namespace Game::Coro::Detail

/**
 * @brief C++20 轻量级生产协程任务句柄（支持值返回、void 返回、嵌套 co_await、主动取消与 Fire-and-Forget）
 * @tparam T 协程返回值类型（默认为 void）
 */
template <typename T>
class TGameCoroTask {
public:
    using promise_type = Game::Coro::Detail::TGameCoroPromise<T>;

public:
    /**
     * @brief 基于内部共享状态构造协程任务句柄
     * @param InState 共享状态引用
     */
    explicit TGameCoroTask(TSharedRef<TGameCoroState<T>, ESPMode::ThreadSafe> InState) noexcept
        : State(MoveTemp(InState)) {
    }

    /**
     * @brief 查询当前协程状态（Running / Completed / Cancelled）
     * @return 协程状态枚举
     */
    [[nodiscard]] EGameCoroStatus GetStatus() const noexcept {
        return State->GetStatus();
    }

    /**
     * @brief 查询协程是否已成功执行完毕
     * @return 已成功完成返回 true
     */
    [[nodiscard]] bool IsCompleted() const noexcept {
        return State->GetStatus() == EGameCoroStatus::Completed;
    }

    /**
     * @brief 查询协程是否已因取消或宿主销毁而中止
     * @return 已取消返回 true
     */
    [[nodiscard]] bool IsCancelled() const noexcept {
        return State->GetStatus() == EGameCoroStatus::Cancelled;
    }

    /**
     * @brief 查询协程是否已结束（Completed 或 Cancelled）
     * @return 已结束返回 true
     */
    [[nodiscard]] bool IsDone() const noexcept {
        return State->GetStatus() != EGameCoroStatus::Running;
    }

    /**
     * @brief 向协程发送取消请求（将在下一个挂起点恢复前拦截并安全销毁协程帧）
     * @return 首次触发取消返回 true
     */
    bool Cancel() noexcept {
        return State->RequestCancel();
    }

    /**
     * @brief 从外部为协程绑定或更新宿主 UObject 与取消令牌（若绑定时已失效则立即触发取消与挂起帧销毁）
     * @param InOwner 宿主对象指针
     * @param InToken 协同取消令牌
     * @note 本接口为外部事后绑定；若需确保协程首段同步逻辑同样受宿主存活保护，请在协程函数首行使用 co_await Game::Coro::BindContext(InOwner, InToken)
     */
    void BindContext(UObject* InOwner, FCancellationToken InToken = FCancellationToken::None()) noexcept {
        State->BindExecutionContext(TWeakObjectPtr<UObject>(InOwner), true, MoveTemp(InToken));
        if (State->ShouldAbort()) {
            State->RequestCancel();
        }
    }

    /**
     * @brief 获取协程执行结果（Completed 返回 Success，Cancelled 返回 Cancelled 错误码）
     * @return TGameStatusResult<T> 结果对象
     */
    [[nodiscard]] TGameStatusResult<T> GetResult() const {
        return State->GetResult();
    }

    /**
     * @brief 注册当协程结束（完成或取消）时触发的回调（若已结束则立即在当前线程调用）
     * @param InCallback 接收 TGameStatusResult<T> 的回调闭包
     */
    void WhenCompleted(TFunction<void(TGameStatusResult<T>)> InCallback) const {
        if (!InCallback) {
            return;
        }
        TSharedRef<TGameCoroState<T>, ESPMode::ThreadSafe> CapturedState = State;
        State->AddCompletionCallback(
            [CapturedState, Callback = MoveTemp(InCallback)]([[maybe_unused]] EGameCoroStatus FinalStatus) {
                Callback(CapturedState->GetResult());
            }
        );
    }

    /**
     * @brief 获取底层线程安全共享状态引用
     * @return 共享状态引用
     */
    [[nodiscard]] const TSharedRef<TGameCoroState<T>, ESPMode::ThreadSafe>& GetSharedState() const noexcept {
        return State;
    }

private:
    TSharedRef<TGameCoroState<T>, ESPMode::ThreadSafe> State;
};

namespace Game::Coro::Detail {

template <typename T>
TGameCoroTask<T> TGameCoroPromise<T>::get_return_object() noexcept {
    return TGameCoroTask<T>(this->State);
}

inline TGameCoroTask<void> TGameCoroPromise<void>::get_return_object() noexcept {
    return TGameCoroTask<void>(this->State);
}

} // namespace Game::Coro::Detail
