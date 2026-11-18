/**
 * @file GameAssetTypes.h
 * @brief 资产加载模块通用类型、枚举与工具声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/StreamableManager.h"

#include "GameAssetTypes.generated.h"

/**
 * @brief 业务语义化资产异步加载优先级枚举
 */
UENUM(BlueprintType)
enum class EGameAssetPriority : uint8 {
    /** 低优先级（背景装饰、长距离次要资产） */
    Low UMETA(DisplayName = "Low"),
    /** 普通优先级（默认场景资产、常规实体） */
    Normal UMETA(DisplayName = "Normal"),
    /** 高优先级（当前玩家关键动作资产、战斗即时特效） */
    High UMETA(DisplayName = "High"),
    /** 关键优先级（阻断性主界面、关键核心实体） */
    Critical UMETA(DisplayName = "Critical")
};

/**
 * @brief 将业务语义优先级转换为引擎底层流式加载优先级数值
 * @param Priority 业务语义优先级
 * @return 引擎底层 TAsyncLoadPriority 优先级整数
 */
[[nodiscard]] constexpr TAsyncLoadPriority ToAsyncLoadPriority(EGameAssetPriority Priority) noexcept {
    switch (Priority) {
        case EGameAssetPriority::Low:
            return -50;
        case EGameAssetPriority::Normal:
            return FStreamableManager::DefaultAsyncLoadPriority;
        case EGameAssetPriority::High:
            return 50;
        case EGameAssetPriority::Critical:
            return 100;
        default:
            return FStreamableManager::DefaultAsyncLoadPriority;
    }
}
