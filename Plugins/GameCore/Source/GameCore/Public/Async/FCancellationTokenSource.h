/**
 * @file FCancellationTokenSource.h
 * @brief 异步取消信号发生源与级联联合取消控制器
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FCancellationToken.h"

/**
 * @brief 异步取消信号发生源（负责签发只读令牌、主动广播触发取消与级联联动）
 */
class GAMECORE_API FCancellationTokenSource {
public:
    FCancellationTokenSource();

    ~FCancellationTokenSource() = default;

    FCancellationTokenSource(const FCancellationTokenSource&) = delete;
    FCancellationTokenSource& operator=(const FCancellationTokenSource&) = delete;

    /**
     * @brief 移动构造函数
     * @param Other 源实例
     */
    FCancellationTokenSource(FCancellationTokenSource&& Other) noexcept = default;

    /**
     * @brief 移动赋值操作符
     * @param Other 源实例
     * @return 自身引用
     */
    FCancellationTokenSource& operator=(FCancellationTokenSource&& Other) noexcept = default;

    /**
     * @brief 获取与当前取消源关联的只读取消令牌
     * @return 值语义取消令牌
     */
    [[nodiscard]] FCancellationToken GetToken() const noexcept;

    /**
     * @brief 判定当前取消源是否已被触发取消
     * @return 已触发返回 true
     */
    [[nodiscard]] bool IsCancellationRequested() const noexcept;

    /**
     * @brief 广播触发取消信号并出锁派发所有关联回调
     * @return 首次触发返回 true，重复调用返回 false
     */
    bool Cancel();

    /**
     * @brief 创建级联联合取消源（任一输入令牌触发取消时，衍生源自动被触发取消）
     * @param TokenA 第一输入令牌
     * @param TokenB 第二输入令牌
     * @return 联动派生出的共享取消源指针
     */
    [[nodiscard]] static TSharedRef<FCancellationTokenSource> CreateLinkedTokenSource(
        const FCancellationToken& TokenA,
        const FCancellationToken& TokenB
    );

private:
    TSharedRef<FCancellationState> State;
    TArray<FCancellationTokenRegistration> LinkedRegistrations;
};
