/**
 * @file FWaitLoadingCompleteAwaiter.h
 * @brief 关卡加载与异步就绪屏障完成 C++20 协程挂起等待器声明（零堆分配直通、跨线程取消安全与全通道防泄漏）
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FCancellationToken.h"
#include "Async/TGameCoroTask.h"
#include "UObject/WeakObjectPtrTemplates.h"

class ULoadingSubsystem;

/**
 * @brief 等待关卡加载幕布与就绪屏障解除的 C++20 协程等待器（支持协同取消、超时、跨线程安全唤醒与子系统销毁中止）
 */
class GAMEFLOW_API FWaitLoadingCompleteAwaiter {
public:
    /**
     * @brief 构造关卡加载完成等待器（零堆分配）
     * @param InSubsystem 加载子系统指针
     * @param InCancellationToken 异步协同取消令牌（默认不可取消）
     * @param InTimeoutSeconds 超时秒数（<=0 或非有限值表示不设超时无限等待）
     */
    explicit FWaitLoadingCompleteAwaiter(
        ULoadingSubsystem* InSubsystem,
        FCancellationToken InCancellationToken = FCancellationToken::None(),
        float InTimeoutSeconds = 0.0f
    ) noexcept;

    FWaitLoadingCompleteAwaiter(const FWaitLoadingCompleteAwaiter&) = delete;
    FWaitLoadingCompleteAwaiter& operator=(const FWaitLoadingCompleteAwaiter&) = delete;

    /**
     * @brief 移动构造函数
     * @param Other 源等待器
     */
    FWaitLoadingCompleteAwaiter(FWaitLoadingCompleteAwaiter&& Other) noexcept;

    /**
     * @brief 移动赋值运算符
     * @param Other 源等待器
     * @return 自身引用
     */
    FWaitLoadingCompleteAwaiter& operator=(FWaitLoadingCompleteAwaiter&& Other) noexcept;

    /**
     * @brief 析构函数
     */
    ~FWaitLoadingCompleteAwaiter();

    /**
     * @brief 若令牌已预取消、子系统为空或当前非加载态则零堆分配直接返回 true 避免无谓挂起
     * @return 无需挂起返回 true
     */
    [[nodiscard]] bool await_ready() const noexcept;

    /**
     * @brief 挂起协程并建立加载完成、子系统销毁中止、定时超时、取消令牌与协程自身取消五路联动监听
     * @param InGuard 挂起点协程帧安全守卫
     */
    void SuspendWithGuard(TSharedRef<Game::Coro::FGameCoroSuspensionGuard, ESPMode::ThreadSafe> InGuard);

    /**
     * @brief 提取等待结果
     * @return 加载正常完成或当前非加载态返回 true；预取消、中途取消、超时、上下文无效或子系统销毁中止返回 false
     */
    [[nodiscard]] bool await_resume() const noexcept;

private:
    struct FWaitLoadingState;

    TWeakObjectPtr<ULoadingSubsystem> SubsystemWeak;
    FCancellationToken CancellationToken;
    float TimeoutSeconds = 0.0f;
    TSharedPtr<FWaitLoadingState, ESPMode::ThreadSafe> WaitState;
    mutable bool bReadyResult = false;
};

namespace GameFlow {

/**
 * @brief 等待关卡加载幕布与就绪屏障完全解除（C++20 协程挂起原语）
 * @param WorldContextObject 世界或实例上下文对象
 * @param CancellationToken 异步协同取消令牌（默认不可取消）
 * @param TimeoutSeconds 超时秒数（默认 0.0f 表示不设超时）
 * @return 挂起等待器（加载正常完成交付 true；取消、超时、上下文无效或子系统销毁中止交付 false）
 */
[[nodiscard]] GAMEFLOW_API FWaitLoadingCompleteAwaiter WaitLoadingComplete(
    const UObject* WorldContextObject,
    FCancellationToken CancellationToken = FCancellationToken::None(),
    float TimeoutSeconds = 0.0f
);

} // namespace GameFlow
