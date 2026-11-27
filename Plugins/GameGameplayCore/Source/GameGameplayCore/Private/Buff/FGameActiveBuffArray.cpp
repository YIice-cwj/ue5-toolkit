/**
 * @file FGameActiveBuffArray.cpp
 * @brief 活动 Buff 的增量复制容器实现
 */

#include "Buff/FGameActiveBuffArray.h"

#include "Buff/UGameBuffComponent.h"
#include "Stat/UGameStatComponent.h"

/**
 * @brief 客户端收到新增条目后：批量挂接修正器、刷新 Tick 状态并广播差异
 * @param AddedIndices 新增条目下标
 * @param FinalSize 复制后的条目总数
 */
void FGameActiveBuffArray::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, [[maybe_unused]] int32 FinalSize) {
    UGameBuffComponent* OwningComponent = OwnerComponent.Get();
    if (OwningComponent == nullptr) {
        return;
    }

    OwningComponent->RefreshTickState();
    TOptional<UGameStatComponent::FScopedModifierBatch> Batch;
    if (UGameStatComponent* StatComp = OwningComponent->ResolveStatComponent()) {
        Batch.Emplace(*StatComp);
    }
    for (const int32 Index : AddedIndices) {
        if (Items.IsValidIndex(Index)) {
            OwningComponent->RegisterInstanceModifiers(Items[Index]);
        }
    }
    Batch.Reset();
    OwningComponent->PublishReplicatedDiff(Items);
}

/**
 * @brief 客户端收到条目变更后：批量原子同步新 Spec 与叠层修正器、刷新 Tick 状态并广播差异
 * @param ChangedIndices 变更条目下标
 * @param FinalSize 复制后的条目总数
 */
void FGameActiveBuffArray::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, [[maybe_unused]] int32 FinalSize) {
    UGameBuffComponent* OwningComponent = OwnerComponent.Get();
    if (OwningComponent == nullptr) {
        return;
    }

    OwningComponent->RefreshTickState();
    TArray<FGameplayTag, TInlineAllocator<8>> ChangedTags;
    TOptional<UGameStatComponent::FScopedModifierBatch> Batch;
    if (UGameStatComponent* StatComp = OwningComponent->ResolveStatComponent()) {
        Batch.Emplace(*StatComp);
    }
    for (const int32 Index : ChangedIndices) {
        if (!Items.IsValidIndex(Index)) {
            continue;
        }
        if (Items[Index].Spec.BuffTag.IsValid()) {
            ChangedTags.AddUnique(Items[Index].Spec.BuffTag);
        }
        OwningComponent->RegisterInstanceModifiers(Items[Index]);
    }
    Batch.Reset();
    OwningComponent->PublishReplicatedDiff(Items, TConstArrayView<int32>(), ChangedTags);
}

/**
 * @brief 客户端移除前：批量回滚被删条目的属性修正器、当帧刷新 Tick 状态并广播差异
 * @param RemovedIndices 待移除条目下标
 * @param FinalSize 移除后的条目总数
 */
void FGameActiveBuffArray::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, [[maybe_unused]] int32 FinalSize) {
    UGameBuffComponent* OwningComponent = OwnerComponent.Get();
    if (OwningComponent == nullptr) {
        return;
    }

    OwningComponent->RefreshTickState(RemovedIndices);
    TOptional<UGameStatComponent::FScopedModifierBatch> Batch;
    if (UGameStatComponent* StatComp = OwningComponent->ResolveStatComponent()) {
        Batch.Emplace(*StatComp);
    }
    for (const int32 Index : RemovedIndices) {
        if (Items.IsValidIndex(Index)) {
            OwningComponent->UnregisterInstanceModifiers(Items[Index]);
        }
    }
    Batch.Reset();
    OwningComponent->PublishReplicatedDiff(Items, RemovedIndices);
}
