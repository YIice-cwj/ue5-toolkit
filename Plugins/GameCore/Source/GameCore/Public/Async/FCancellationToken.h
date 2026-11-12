/**
 * @file FCancellationToken.h
 * @brief 纯值语义异步协同取消令牌与取消源基础设施（支持级联取消、出锁回调派发与零锁状态轮询）
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"

#include <atomic>

class FCancellationTokenSource;

/**
 * @brief 取消回调注销控制句柄（支持析构自动注销与主动解除）
 */
class GAMECORE_API FCancellationTokenRegistration {
public:
    FCancellationTokenRegistration() = default;

    /**
     * @brief 构造包含有效绑定的注销句柄
     * @param InState 内部状态共享指针
     * @param InCallbackId 回调唯一序号
     */
    FCancellationTokenRegistration(TWeakPtr<class FCancellationState> InState, uint64 InCallbackId) noexcept;

    ~FCancellationTokenRegistration();

    FCancellationTokenRegistration(const FCancellationTokenRegistration&) = delete;
    FCancellationTokenRegistration& operator=(const FCancellationTokenRegistration&) = delete;

    /**
     * @brief 移动构造函数
     * @param Other 源注销句柄
     */
    FCancellationTokenRegistration(FCancellationTokenRegistration&& Other) noexcept;

    /**
     * @brief 移动赋值操作符
     * @param Other 源注销句柄
     * @return 自身引用
     */
    FCancellationTokenRegistration& operator=(FCancellationTokenRegistration&& Other) noexcept;

    /**
     * @brief 主动解绑注销回调
     */
    void Unregister() noexcept;

    /**
     * @brief 查询当前句柄是否仍持有有效的注册回调
     * @return 处于注册态返回 true
     */
    [[nodiscard]] bool IsRegistered() const noexcept {
        return CallbackId != 0;
    }

private:
    TWeakPtr<class FCancellationState> WeakState;
    uint64 CallbackId = 0;
};

/**
 * @brief 内部取消共享状态（承载原子取消标志、读写锁与出锁派发列表）
 */
class GAMECORE_API FCancellationState : public TSharedFromThis<FCancellationState> {
public:
    struct FCallbackEntry {
        uint64 Id = 0;
        TFunction<void()> Action;
    };

public:
    FCancellationState() = default;

    /**
     * @brief 查询是否已触发取消请求
     * @return 已取消返回 true
     */
    [[nodiscard]] bool IsCancellationRequested() const noexcept {
        return bCancelled.load(std::memory_order_acquire);
    }

    /**
     * @brief 触发取消并无锁派发所有已注册的回调（严格遵循出锁派发铁律）
     * @return 首次触发返回 true，若此前已处于取消态返回 false
     */
    bool RequestCancel();

    /**
     * @brief 注册取消监听回调（若已处于取消态则出锁立即同步调用）
     * @param Callback 待派发的回调闭包
     * @return 注销控制句柄
     */
    FCancellationTokenRegistration RegisterCallback(TFunction<void()> Callback);

    /**
     * @brief 根据序号移除已注册的回调
     * @param CallbackId 目标回调序号
     */
    void UnregisterCallback(uint64 CallbackId) noexcept;

private:
    std::atomic<bool> bCancelled{false};
    mutable FRWLock Lock;
    TArray<FCallbackEntry> Callbacks;
    uint64 NextCallbackId = 1;
};

/**
 * @brief 纯值语义异步协同取消令牌
 */
class GAMECORE_API FCancellationToken {
public:
    FCancellationToken() noexcept = default;

    /**
     * @brief 基于共享状态构造取消令牌
     * @param InState 状态指针
     */
    explicit FCancellationToken(TSharedPtr<FCancellationState> InState) noexcept
        : State(MoveTemp(InState)) {}

    /**
     * @brief 查询当前令牌是否已收到取消请求（零锁原子轮询）
     * @return 已被请求取消返回 true
     */
    [[nodiscard]] bool IsCancellationRequested() const noexcept {
        return State.IsValid() && State->IsCancellationRequested();
    }

    /**
     * @brief 查询当前令牌是否具备被取消的可能（未绑定源时恒为 false）
     * @return 具备取消能力返回 true
     */
    [[nodiscard]] bool CanBeCancelled() const noexcept {
        return State.IsValid();
    }

    /**
     * @brief 注册当取消发生时执行的响应回调
     * @param Callback 取消回调闭包
     * @return 注销控制句柄
     */
    FCancellationTokenRegistration Register(TFunction<void()> Callback) const {
        if (!State.IsValid()) {
            return FCancellationTokenRegistration();
        }
        return State->RegisterCallback(MoveTemp(Callback));
    }

    /**
     * @brief 布尔显式类型转换操作符（已被请求取消返回 true）
     * @return 已取消返回 true
     */
    [[nodiscard]] explicit operator bool() const noexcept {
        return IsCancellationRequested();
    }

    /**
     * @brief 令牌相等比较操作符（比较内部共享状态同一性）
     * @param Other 待比较令牌
     * @return 状态源同一返回 true
     */
    [[nodiscard]] bool operator==(const FCancellationToken& Other) const noexcept {
        return State == Other.State;
    }

    /**
     * @brief 令牌不相等比较操作符
     * @param Other 待比较令牌
     * @return 状态源不相同返回 true
     */
    [[nodiscard]] bool operator!=(const FCancellationToken& Other) const noexcept {
        return State != Other.State;
    }

    /**
     * @brief 获取不可被取消的全局空令牌单例
     * @return 恒为 false 的令牌对象
     */
    [[nodiscard]] static FCancellationToken None() noexcept {
        static const FCancellationToken StaticNone;
        return StaticNone;
    }

private:
    TSharedPtr<FCancellationState> State;
};
