/**
 * @file FGameActiveBuffArray.cpp
 * @brief 活动 Buff 的增量复制容器实现
 */

#include "Buff/FGameActiveBuffArray.h"

#include "Buff/UGameBuffComponent.h"

void FGameActiveBuffArray::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize) {
    if (OwnerComponent == nullptr) {
        return;
    }

    for (const int32 Index : AddedIndices) {
        if (Items.IsValidIndex(Index)) {
            OwnerComponent->RegisterInstanceModifiers(Items[Index]);
        }
    }
    OwnerComponent->RefreshTickState();
    OwnerComponent->PublishReplicatedDiff(Items);
}

void FGameActiveBuffArray::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize) {
    if (OwnerComponent == nullptr) {
        return;
    }

    for (const int32 Index : ChangedIndices) {
        if (Items.IsValidIndex(Index)) {
            OwnerComponent->UnregisterInstanceModifiers(Items[Index]);
            OwnerComponent->RegisterInstanceModifiers(Items[Index]);
        }
    }
    OwnerComponent->RefreshTickState();
    OwnerComponent->PublishReplicatedDiff(Items);
}

void FGameActiveBuffArray::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize) {
    if (OwnerComponent == nullptr) {
        return;
    }

    for (const int32 Index : RemovedIndices) {
        if (Items.IsValidIndex(Index)) {
            OwnerComponent->UnregisterInstanceModifiers(Items[Index]);
        }
    }
    OwnerComponent->PublishReplicatedDiff(Items, RemovedIndices);
}

