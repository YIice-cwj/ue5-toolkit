/**
 * @file FGameActiveBuffArray.cpp
 * @brief 活动 Buff 的增量复制容器实现
 */

#include "Buff/FGameActiveBuffArray.h"

#include "Buff/UGameBuffComponent.h"

void FGameActiveBuffArray::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize) {
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

void FGameActiveBuffArray::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize) {
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

void FGameActiveBuffArray::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize) {
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

