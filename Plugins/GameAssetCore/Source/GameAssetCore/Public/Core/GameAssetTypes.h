/**
 * @file GameAssetTypes.h
 * @brief 资产加载模块通用优先级枚举与底层流式优先级转换工具声明
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"

#include "GameAssetTypes.generated.h"

/**
 * @brief 业务语义化资产异步加载优先级枚举
 */
UENUM(BlueprintType)
enum class EGameAssetPriority : uint8 {
    /**
     * @brief 低优先级（背景装饰、远景次要资产）
     */
    Low UMETA(DisplayName = "Low"),

    /**
     * @brief 普通优先级（默认场景资产、常规实体）
     */
    Normal UMETA(DisplayName = "Normal"),

    /**
     * @brief 高优先级（当前玩家关键动作资产、战斗即时表现）
     */
    High UMETA(DisplayName = "High"),

    /**
     * @brief 关键优先级（阻断性主界面、关键核心实体）
     */
    Critical UMETA(DisplayName = "Critical")
};

namespace Game::AssetPriorityValues {

/**
 * @brief 低优先级对应的引擎异步加载数值
 */
inline constexpr TAsyncLoadPriority Low = -50;

/**
 * @brief 普通默认优先级对应的引擎异步加载数值
 */
inline constexpr TAsyncLoadPriority Normal = 0;

/**
 * @brief 高优先级对应的引擎异步加载数值
 */
inline constexpr TAsyncLoadPriority High = 50;

/**
 * @brief 关键最高优先级对应的引擎异步加载数值
 */
inline constexpr TAsyncLoadPriority Critical = 100;

}  // namespace Game::AssetPriorityValues

/**
 * @brief 将业务语义优先级转换为引擎底层流式加载优先级数值
 * @param Priority 业务语义优先级
 * @return 引擎底层 TAsyncLoadPriority 优先级整数
 */
[[nodiscard]] constexpr TAsyncLoadPriority ToAsyncLoadPriority(EGameAssetPriority Priority) noexcept {
    switch (Priority) {
        case EGameAssetPriority::Low:
            return Game::AssetPriorityValues::Low;
        case EGameAssetPriority::Normal:
            return Game::AssetPriorityValues::Normal;
        case EGameAssetPriority::High:
            return Game::AssetPriorityValues::High;
        case EGameAssetPriority::Critical:
            return Game::AssetPriorityValues::Critical;
        default:
            return Game::AssetPriorityValues::Normal;
    }
}
