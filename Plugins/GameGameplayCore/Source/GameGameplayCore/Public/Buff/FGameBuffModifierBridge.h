/**
 * @file FGameBuffModifierBridge.h
 * @brief Buff 到数值组件的修正器桥接器（纯 C++ · 原子原地更新、刷新零抖动、按层数缩放挂接与句柄精确回滚）
 */

#pragma once

#include "CoreMinimal.h"
#include "Buff/FGameBuffTypes.h"

class UGameStatComponent;
struct FGameBuffInstance;

/**
 * @brief Buff 修正器桥接器
 */
struct GAMEGAMEPLAYCORE_API FGameBuffModifierBridge {
    /**
     * @brief 按层数缩放后原子挂接或原地更新实例的全部修正器（相同配置跳过重建，叠层原地更新无中间态抖动）
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
     * @brief 从指定数值组件批量回滚全部已记录修正器并清空句柄映射
     * @param InStatComponent 原挂接的数值组件
     */
    void ClearAll(UGameStatComponent& InStatComponent);

    /**
     * @brief 从指定数值组件静默批量回滚全部已记录修正器并清空句柄映射（不触发任何业务属性广播，专供对象池归还使用）
     * @param InStatComponent 原挂接的数值组件
     */
    void ClearAllSilent(UGameStatComponent& InStatComponent);

    /**
     * @brief 清空全部句柄映射（仅在原数值组件已不可用时使用，不执行回滚）
     */
    void Reset();

private:
    using FAppliedModifierArray = TArray<FGameBuffAppliedModifier, TInlineAllocator<2>>;

    TMap<int32, FAppliedModifierArray> HandlesByInstance;
};
