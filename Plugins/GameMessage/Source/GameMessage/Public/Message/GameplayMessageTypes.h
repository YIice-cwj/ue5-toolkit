/**
 * @file GameplayMessageTypes.h
 * @brief 消息总线公共枚举与反射结构体编译期概念约束定义
 */

#pragma once

#include <concepts>
#include <type_traits>

#include "CoreMinimal.h"
#include "UObject/Class.h"

#include "GameplayMessageTypes.generated.h"

/**
 * @brief 约束载荷类型必须为受 UE 反射支持的结构体（USTRUCT 或已特化 TBaseStructure 的引擎核心结构体）
 * @tparam T 待校验的载荷类型
 */
template <typename T>
concept CGameplayMessageStruct = requires {
    { TBaseStructure<std::remove_cvref_t<T>>::Get() } -> std::convertible_to<const UScriptStruct*>;
};

/**
 * @brief 消息通道匹配规则
 */
UENUM(BlueprintType)
enum class EGameplayMessageMatch : uint8 {
    /**
     * @brief 精确匹配：仅当广播通道与监听通道完全相同时触发
     */
    ExactMatch,

    /**
     * @brief 部分/前缀匹配：广播通道与监听通道相同或其子标签时均会触发
     */
    PartialMatch
};
