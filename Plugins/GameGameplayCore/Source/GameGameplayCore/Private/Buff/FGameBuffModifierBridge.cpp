/**
 * @file FGameBuffModifierBridge.cpp
 * @brief Buff 修正器桥接器实现
 */

#include "Buff/FGameBuffModifierBridge.h"

#include "Stat/UGameStatComponent.h"

/**
 * @brief 按层数缩放后挂接实例的全部修正器，并记录句柄供回滚
 * @param InInstanceId 运行实例唯一 ID
 * @param InInstance 运行实例（只读，提供 Spec 与层数）
 * @param InStatComponent 目标数值组件
 */
void FGameBuffModifierBridge::Register(int32 InInstanceId, const FGameBuffInstance& InInstance, UGameStatComponent& InStatComponent) {
    Unregister(InInstanceId, &InStatComponent);

    if (InInstance.Spec.Modifiers.IsEmpty()) {
        return;
    }

    TArray<FGameBuffAppliedModifier>& Handles = HandlesByInstance.FindOrAdd(InInstanceId);
    for (const FGameStatModifierSpec& ModifierSpec : InInstance.Spec.Modifiers) {
        FGameStatModifier ScaledModifier = ModifierSpec.Modifier;
        if (InInstance.Stacks > 1) {
            if (ScaledModifier.Op == EGameStatModifierOp::Add) {
                ScaledModifier.Value *= static_cast<float>(InInstance.Stacks);
            } else if (ScaledModifier.Op == EGameStatModifierOp::Multiply) {
                ScaledModifier.Value = 1.0f + (ScaledModifier.Value - 1.0f) * static_cast<float>(InInstance.Stacks);
            }
        }

        FGameBuffAppliedModifier Applied;
        Applied.StatTag = ModifierSpec.StatTag;
        const TGameStatusResult<uint32> AddResult = InStatComponent.AddStatModifier(ModifierSpec.StatTag, ScaledModifier);
        Applied.Handle = AddResult.IsSuccess() ? AddResult.GetValue() : 0;
        Handles.Add(Applied);
    }
}

/**
 * @brief 回滚实例已挂接的全部修正器并移除句柄映射
 * @param InInstanceId 运行实例唯一 ID
 * @param InStatComponent 目标数值组件；为空时仅清理映射并跳过回滚
 */
void FGameBuffModifierBridge::Unregister(int32 InInstanceId, UGameStatComponent* InStatComponent) {
    TArray<FGameBuffAppliedModifier> Handles;
    if (!HandlesByInstance.RemoveAndCopyValue(InInstanceId, Handles)) {
        return;
    }

    if (InStatComponent == nullptr) {
        return;
    }

    for (const FGameBuffAppliedModifier& Applied : Handles) {
        InStatComponent->RemoveStatModifier(Applied.StatTag, Applied.Handle);
    }
}

/**
 * @brief 从指定数值组件回滚全部已记录修正器并清空句柄映射
 * @param InStatComponent 原挂接的数值组件
 */
void FGameBuffModifierBridge::ClearAll(UGameStatComponent& InStatComponent) {
    for (const TPair<int32, TArray<FGameBuffAppliedModifier>>& Pair : HandlesByInstance) {
        for (const FGameBuffAppliedModifier& Applied : Pair.Value) {
            InStatComponent.RemoveStatModifier(Applied.StatTag, Applied.Handle);
        }
    }
    Reset();
}

/**
 * @brief 清空全部句柄映射（仅在原数值组件已不可用时使用，不执行回滚）
 */
void FGameBuffModifierBridge::Reset() {
    HandlesByInstance.Reset();
}
