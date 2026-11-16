/**
 * @file FGameActiveBuffArray.cpp
 * @brief 活动 Buff 的增量复制容器实现
 */

#include "Buff/FGameActiveBuffArray.h"

#include "Buff/UGameBuffComponent.h"

/**
 * @brief 客户端收到新增条目后：挂接修正器、刷新 Tick 状态并广播差异
 * @param AddedIndices 新增条目下标
 * @param FinalSize 复制后的条目总数
 */
void FGameActiveBuffArray::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, [[maybe_unused]] int32 FinalSize) {
    UGameBuffComponent* OwningComponent = OwnerComponent.Get();
    if (OwningComponent == nullptr) {
        return;
    }

    for (const int32 Index : AddedIndices) {
        if (Items.IsValidIndex(Index)) {
            OwningComponent->RegisterInstanceModifiers(Items[Index]);
        }
    }
    OwningComponent->RefreshTickState();
    OwningComponent->PublishReplicatedDiff(Items);
}

/**
 * @brief 客户端收到条目变更后：按新 Spec 与叠层重建修正器、刷新 Tick 状态并广播差异
 * @param ChangedIndices 变更条目下标
 * @param FinalSize 复制后的条目总数
 */
void FGameActiveBuffArray::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, [[maybe_unused]] int32 FinalSize) {
    UGameBuffComponent* OwningComponent = OwnerComponent.Get();
    if (OwningComponent == nullptr) {
        return;
    }

    TArray<FGameplayTag> ChangedTags;
    for (const int32 Index : ChangedIndices) {
        if (Items.IsValidIndex(Index)) {
            ChangedTags.AddUnique(Items[Index].Spec.BuffTag);
            OwningComponent->UnregisterInstanceModifiers(Items[Index]);
            OwningComponent->RegisterInstanceModifiers(Items[Index]);
        }
    }
    OwningComponent->RefreshTickState();
    OwningComponent->PublishReplicatedDiff(Items, TArrayView<const int32>(), ChangedTags);
}

/**
 * @brief 客户端移除前：先回滚被删条目的属性修正器，再广播差异
 * @param RemovedIndices 待移除条目下标
 * @param FinalSize 移除后的条目总数
 */
void FGameActiveBuffArray::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, [[maybe_unused]] int32 FinalSize) {
    UGameBuffComponent* OwningComponent = OwnerComponent.Get();
    if (OwningComponent == nullptr) {
        return;
    }

    for (const int32 Index : RemovedIndices) {
        if (Items.IsValidIndex(Index)) {
            OwningComponent->UnregisterInstanceModifiers(Items[Index]);
        }
    }
    OwningComponent->PublishReplicatedDiff(Items, RemovedIndices);
}
