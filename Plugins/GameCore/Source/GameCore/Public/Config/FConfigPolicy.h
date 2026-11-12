// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 配置策略规则管理（只读键拦截、环境只读判定与发布期特权限制）
 */
class GAMECORE_API FConfigPolicy {
public:
    FConfigPolicy() = delete;

    /**
     * @brief 判定指定键名在当前编译开关与运行时环境下是否属于固有只读键
     * @param Name 配置键名
     * @return 固有只读返回 true
     */
    [[nodiscard]] static bool IsBuiltinReadOnlyKey(FName Name) noexcept;
};
