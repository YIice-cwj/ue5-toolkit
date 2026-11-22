// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file FRemoteLogReentrancyScope.h
 * @brief 内部线程局部重入安全守卫声明（阻断日志拦截、排空与总线消费过程中的二次重入雪崩）
 */

#pragma once

#include "CoreMinimal.h"

namespace Game::Log::Private {

/**
 * @brief 线程局部重入安全守卫（模块私有实现，支持嵌套作用域无损恢复）
 */
class FRemoteLogReentrancyScope final {
public:
    /**
     * @brief 构造守卫并标记当前线程进入远程日志处理重入上下文
     */
    FRemoteLogReentrancyScope() noexcept;

    /**
     * @brief 析构守卫并恢复进入前的重入标记状态
     */
    ~FRemoteLogReentrancyScope() noexcept;

    FRemoteLogReentrancyScope(const FRemoteLogReentrancyScope&) = delete;
    FRemoteLogReentrancyScope& operator=(const FRemoteLogReentrancyScope&) = delete;
    FRemoteLogReentrancyScope(FRemoteLogReentrancyScope&&) = delete;
    FRemoteLogReentrancyScope& operator=(FRemoteLogReentrancyScope&&) = delete;

    /**
     * @brief 查询当前调用线程是否正处于远程日志拦截或派发重入上下文中
     * @return 处于重入上下文返回 true
     */
    [[nodiscard]] static bool IsHandling() noexcept;

private:
    bool bPreviousState = false;
};

} // namespace Game::Log::Private
