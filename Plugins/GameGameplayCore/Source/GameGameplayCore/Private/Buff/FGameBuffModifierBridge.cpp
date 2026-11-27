/**
 * @file FGameBuffModifierBridge.cpp
 * @brief Buff 修正器桥接器实现
 */

#include "Buff/FGameBuffModifierBridge.h"

#include "Buff/FGameBuffInstance.h"
#include "Stat/UGameStatComponent.h"

namespace {

using FAppliedModifierArray = TArray<FGameBuffAppliedModifier, TInlineAllocator<2>>;

/**
 * @brief 按 Buff 当前层数对单条属性修正器执行数值缩放（加算线性缩放、乘算按增量线性缩放、覆写不缩放）
 * @param InModifier 原始单层修正器
 * @param InStacks 当前有效层数
 * @return 缩放后的修正器副本
 */
FGameStatModifier BuildScaledModifier(const FGameStatModifier& InModifier, int32 InStacks) noexcept {
    FGameStatModifier Scaled = InModifier;
    if (InStacks <= 1) {
        return Scaled;
    }

    if (Scaled.Op == EGameStatModifierOp::Add) {
        Scaled.Value *= static_cast<float>(InStacks);
    } else if (Scaled.Op == EGameStatModifierOp::Multiply) {
        Scaled.Value = 1.0f + (Scaled.Value - 1.0f) * static_cast<float>(InStacks);
    }
    return Scaled;
}

/**
 * @brief 检查已有已应用修正器数组与目标缩放规格是否完全一致（用于满层刷新零开销短路）
 * @param InExisting 已有已应用修正器数组
 * @param InValidScaledSpecs 本次待应用的有效缩放修正规格切片
 * @return 数量、标签顺序与缩放后规则全部等价返回 true
 */
bool AreAppliedModifiersIdentical(
    const FAppliedModifierArray& InExisting,
    TConstArrayView<FGameStatModifierSpec> InValidScaledSpecs
) noexcept {
    if (InExisting.Num() != InValidScaledSpecs.Num()) {
        return false;
    }

    for (int32 Index = 0; Index < InValidScaledSpecs.Num(); ++Index) {
        const FGameBuffAppliedModifier& Applied = InExisting[Index];
        if (!Applied.StatTag.MatchesTagExact(InValidScaledSpecs[Index].StatTag)
            || !(Applied.AppliedModifier == InValidScaledSpecs[Index].Modifier)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 在外部已开启的批处理作用域内，尝试按相同属性标签布局原地原子更新已有修正器槽位
 * @param InOutExisting 已有已应用修正器数组引用
 * @param InValidScaledSpecs 本次待应用的有效缩放修正规格切片
 * @param InStatComponent 目标数值组件引用
 * @return 全部槽位成功原地更新返回 true；标签布局变化或任一句柄失效需回退重建返回 false
 */
bool TryUpdateExistingModifiersInPlace(
    FAppliedModifierArray& InOutExisting,
    TConstArrayView<FGameStatModifierSpec> InValidScaledSpecs,
    UGameStatComponent& InStatComponent
) {
    if (InOutExisting.Num() != InValidScaledSpecs.Num()) {
        return false;
    }

    for (int32 Index = 0; Index < InValidScaledSpecs.Num(); ++Index) {
        if (!InOutExisting[Index].StatTag.MatchesTagExact(InValidScaledSpecs[Index].StatTag)) {
            return false;
        }
    }

    for (int32 Index = 0; Index < InValidScaledSpecs.Num(); ++Index) {
        FGameBuffAppliedModifier& Applied = InOutExisting[Index];
        if (Applied.AppliedModifier == InValidScaledSpecs[Index].Modifier) {
            continue;
        }
        if (InStatComponent.UpdateStatModifier(Applied.StatTag, Applied.Handle, InValidScaledSpecs[Index].Modifier).IsFailure()) {
            return false;
        }
        Applied.AppliedModifier = InValidScaledSpecs[Index].Modifier;
    }
    return true;
}

}  // namespace

/**
 * @brief 按层数缩放后原子挂接或原地更新实例的全部修正器（相同配置跳过重建，叠层原地更新无中间态抖动）
 * @param InInstanceId 运行实例唯一 ID
 * @param InInstance 运行实例（只读，提供 Spec 与层数）
 * @param InStatComponent 目标数值组件
 */
void FGameBuffModifierBridge::Register(
    int32 InInstanceId,
    const FGameBuffInstance& InInstance,
    UGameStatComponent& InStatComponent
) {
    if (InInstance.Spec.Modifiers.IsEmpty()) {
        Unregister(InInstanceId, &InStatComponent);
        return;
    }

    TArray<FGameStatModifierSpec, TInlineAllocator<4>> ValidScaledSpecs;
    ValidScaledSpecs.Reserve(InInstance.Spec.Modifiers.Num());
    for (const FGameStatModifierSpec& ModifierSpec : InInstance.Spec.Modifiers) {
        if (!ModifierSpec.StatTag.IsValid()) {
            continue;
        }
        FGameStatModifierSpec ScaledSpec;
        ScaledSpec.StatTag = ModifierSpec.StatTag;
        ScaledSpec.Modifier = BuildScaledModifier(ModifierSpec.Modifier, InInstance.Stacks);
        if (!FMath::IsFinite(ScaledSpec.Modifier.Value)) {
            continue;
        }
        ValidScaledSpecs.Add(ScaledSpec);
    }

    if (ValidScaledSpecs.IsEmpty()) {
        Unregister(InInstanceId, &InStatComponent);
        return;
    }

    FAppliedModifierArray* ExistingHandles = HandlesByInstance.Find(InInstanceId);
    if (ExistingHandles != nullptr && AreAppliedModifiersIdentical(*ExistingHandles, ValidScaledSpecs)) {
        return;
    }

    UGameStatComponent::FScopedModifierBatch Batch(InStatComponent);
    if (ExistingHandles != nullptr && TryUpdateExistingModifiersInPlace(*ExistingHandles, ValidScaledSpecs, InStatComponent)) {
        return;
    }

    Unregister(InInstanceId, &InStatComponent);

    FAppliedModifierArray AppliedHandles;
    AppliedHandles.Reserve(ValidScaledSpecs.Num());
    for (const FGameStatModifierSpec& ScaledSpec : ValidScaledSpecs) {
        const TGameStatusResult<FGameStatModifierHandle> AddResult =
            InStatComponent.AddStatModifier(ScaledSpec.StatTag, ScaledSpec.Modifier);
        if (AddResult.IsSuccess()) {
            AppliedHandles.Add(FGameBuffAppliedModifier{ScaledSpec.StatTag, AddResult.GetValue(), ScaledSpec.Modifier});
        }
    }

    if (!AppliedHandles.IsEmpty()) {
        HandlesByInstance.Add(InInstanceId, MoveTemp(AppliedHandles));
    }
}

/**
 * @brief 回滚实例已挂接的全部修正器并移除句柄映射
 * @param InInstanceId 运行实例唯一 ID
 * @param InStatComponent 目标数值组件；为空时仅清理映射并跳过回滚
 */
void FGameBuffModifierBridge::Unregister(int32 InInstanceId, UGameStatComponent* InStatComponent) {
    FAppliedModifierArray Handles;
    if (!HandlesByInstance.RemoveAndCopyValue(InInstanceId, Handles)) {
        return;
    }

    if (InStatComponent == nullptr) {
        return;
    }

    UGameStatComponent::FScopedModifierBatch Batch(*InStatComponent);
    for (const FGameBuffAppliedModifier& Applied : Handles) {
        InStatComponent->RemoveStatModifier(Applied.StatTag, Applied.Handle);
    }
}

/**
 * @brief 从指定数值组件批量回滚全部已记录修正器并清空句柄映射
 * @param InStatComponent 原挂接的数值组件
 */
void FGameBuffModifierBridge::ClearAll(UGameStatComponent& InStatComponent) {
    if (HandlesByInstance.IsEmpty()) {
        return;
    }

    UGameStatComponent::FScopedModifierBatch Batch(InStatComponent);
    for (const TPair<int32, FAppliedModifierArray>& Pair : HandlesByInstance) {
        for (const FGameBuffAppliedModifier& Applied : Pair.Value) {
            InStatComponent.RemoveStatModifier(Applied.StatTag, Applied.Handle);
        }
    }
    Reset();
}

/**
 * @brief 从指定数值组件静默批量回滚全部已记录修正器并清空句柄映射（不触发任何业务属性广播，专供对象池归还使用）
 * @param InStatComponent 原挂接的数值组件
 */
void FGameBuffModifierBridge::ClearAllSilent(UGameStatComponent& InStatComponent) {
    if (HandlesByInstance.IsEmpty()) {
        return;
    }

    for (const TPair<int32, FAppliedModifierArray>& Pair : HandlesByInstance) {
        for (const FGameBuffAppliedModifier& Applied : Pair.Value) {
            InStatComponent.RemoveStatModifierSilent(Applied.StatTag, Applied.Handle);
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
