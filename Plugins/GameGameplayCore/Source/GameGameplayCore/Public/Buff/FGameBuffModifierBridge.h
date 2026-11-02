/**
 * @file FGameBuffModifierBridge.h
 * @brief Buff 到数值组件的修正器桥接器（纯 C++ · 按层数缩放挂接与句柄精确回滚）
 */

#pragma once

#include "CoreMinimal.h"
#include "Buff/FGameActiveBuffArray.h"

class UGameStatComponent;

/**
 * @brief Buff 修正器桥接器
 */
struct GAMEGAMEPLAYCORE_API FGameBuffModifierBridge {
    /**
     * @brief 按层数缩放后挂接实例的全部修正器，并记录句柄供回滚
     * @param InInstanceId 运行实例唯一 ID
     * @param InInstance 运行实例（只读，提供 Spec 与层数）
     * @param InStatComponent 目标数值组件
     */
    void Register(int32 InInstanceId, const FGameBuffInstance& InInstance, UGameStatComponent& InStatComponent);

    /**
     * @brief 回滚实例已挂接的全部修正器并移除句柄映射
     * @param InInstanceId 运行实例唯一 ID
     * @param InStatComponent 目标数值组件；为空时仅清理映射并跳过回滚
     */
    void Unregister(int32 InInstanceId, UGameStatComponent* InStatComponent);

    /**
     * @brief 从指定数值组件回滚全部已记录修正器并清空句柄映射
     * @param InStatComponent 原挂接的数值组件
     */
    void ClearAll(UGameStatComponent& InStatComponent);

    /**
     * @brief 清空全部句柄映射（仅在原数值组件已不可用时使用，不执行回滚）
     */
    void Reset();

private:
    TMap<int32, TArray<FGameBuffAppliedModifier>> HandlesByInstance;
};
