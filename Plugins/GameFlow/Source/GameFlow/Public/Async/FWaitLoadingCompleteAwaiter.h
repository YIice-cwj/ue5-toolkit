/**
 * @file FWaitLoadingCompleteAwaiter.h
 * @brief 关卡加载与异步就绪屏障完成 C++20 协程挂起等待器
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FCancellationToken.h"
#include "Async/TGameCoroTask.h"
#include "Engine/EngineTypes.h"
#include "UObject/WeakObjectPtrTemplates.h"

class ULoadingSubsystem;

/**
 * @brief 等待关卡加载幕布与就绪屏障解除的协程等待器（支持超时、取消令牌与生命周期熔断）
 */
class GAMEFLOW_API FWaitLoadingCompleteAwaiter {
public:
    /**
     * @brief 构造关卡加载完成等待器
     * @param InSubsystem 加载子系统指针
     * @param InTimeoutSeconds 超时秒数（<=0 表示不设超时无限等待）
     * @param InCancellationToken 异步协同取消令牌
     */
    FWaitLoadingCompleteAwaiter(
        ULoadingSubsystem* InSubsystem,
        float InTimeoutSeconds = 0.0f,
        FCancellationToken InCancellationToken = FCancellationToken::None()
    ) noexcept;

    FWaitLoadingCompleteAwaiter(const FWaitLoadingCompleteAwaiter&) = default;
    FWaitLoadingCompleteAwaiter(FWaitLoadingCompleteAwaiter&&) noexcept = default;
    FWaitLoadingCompleteAwaiter& operator=(const FWaitLoadingCompleteAwaiter&) = default;
    FWaitLoadingCompleteAwaiter& operator=(FWaitLoadingCompleteAwaiter&&) noexcept = default;
    ~FWaitLoadingCompleteAwaiter() = default;

    /**
     * @brief 若子系统为空或当前非加载态则直接返回 true 避免无谓挂起
     * @return 无需挂起返回 true
     */
    [[nodiscard]] bool await_ready() const noexcept;

    /**
     * @brief 挂起协程并挂钩加载状态变动委托
     * @param InGuard 挂起点协程帧安全守卫
     */
    void SuspendWithGuard(TSharedRef<Game::Coro::FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard);

    /**
     * @brief 提取等待结果
     * @return 加载正常完成且非加载态返回 true；超时、主动取消或子系统失效返回 false
     */
    [[nodiscard]] bool await_resume() const noexcept;

private:
    TWeakObjectPtr<ULoadingSubsystem> SubsystemWeak;
    float TimeoutSeconds = 0.0f;
    FCancellationToken CancellationToken;
    TSharedPtr<bool, ESPMode::ThreadSafe> ResultHolder;
};

namespace GameFlow {

/**
 * @brief 等待关卡加载幕布与就绪屏障完全解除（C++20 协程挂起原语）
 * @param WorldContextObject 世界上下文对象
 * @param TimeoutSeconds 超时秒数（默认 0.0f 表示不设超时）
 * @param CancellationToken 异步协同取消令牌（默认不可取消）
 * @return 挂起等待器（加载正常完成交付 true，超时、主动取消或上下文无效交付 false）
 */
[[nodiscard]] GAMEFLOW_API FWaitLoadingCompleteAwaiter WaitLoadingComplete(
    const UObject* WorldContextObject,
    float TimeoutSeconds = 0.0f,
    FCancellationToken CancellationToken = FCancellationToken::None()
);

/**
 * @brief 等待关卡加载幕布与就绪屏障完全解除（支持取消令牌优先入参）
 * @param WorldContextObject 世界上下文对象
 * @param CancellationToken 异步协同取消令牌
 * @param TimeoutSeconds 超时秒数（默认 0.0f 表示不设超时）
 * @return 挂起等待器
 */
[[nodiscard]] GAMEFLOW_API inline FWaitLoadingCompleteAwaiter WaitLoadingComplete(
    const UObject* WorldContextObject,
    FCancellationToken CancellationToken,
    float TimeoutSeconds = 0.0f
) {
    return WaitLoadingComplete(WorldContextObject, TimeoutSeconds, MoveTemp(CancellationToken));
}

} // namespace GameFlow
