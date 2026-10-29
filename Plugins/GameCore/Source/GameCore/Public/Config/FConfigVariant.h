/**
 * @file FConfigVariant.h
 * @brief 配置类型擦除变体定义（单一职责 · 强类型支持 · 零堆分配）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Misc/TVariant.h"

namespace Game::Config {

/**
 * @brief 配置类型擦除变体（支持常用标量、容器类型与引擎数学类型；最大成员 24B）
 */
using FConfigVariant = TVariant<bool, int32, int64, float, double, FString, FName, FVector, FRotator, FGameplayTag>;

/**
 * @brief 从变体中按强类型获取目标数据指针
 * @tparam T 目标类型
 * @param InVariant 源配置变体
 * @return 指向内部具体类型的常量指针；若类型不匹配则返回 nullptr
 */
template<typename T>
const T* FindVariantValue(const FConfigVariant& InVariant) {
    return InVariant.TryGet<T>();
}

}  // namespace Game::Config

/**
 * @brief SetConfig 配置写入结果码
 */
enum class ESetConfigResult : uint8 {
    /**
     * @brief 写入成功（包含首次注册与运行时覆盖）
     */
    Applied,

    /**
     * @brief 写入被拒绝（目标键被标记为只读，如 Env.* 或 Shipping 下的 Dev.* 键）
     */
    RejectedReadOnly,
};
