/**
 * @file FGameBuffModifierBridge.cpp
 * @brief Buff 修正器桥接器实现
 */

#include "Buff/FGameBuffModifierBridge.h"

#include "Stat/UGameStatComponent.h"

void FGameBuffModifierBridge::Register(int32 InInstanceId, const FGameBuffInstance& InInstance, UGameStatComponent& InStatComponent) {
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
        Applied.Handle = InStatComponent.AddStatModifier(ModifierSpec.StatTag, ScaledModifier);
        Handles.Add(Applied);
    }
}

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

void FGameBuffModifierBridge::ClearAll(UGameStatComponent& InStatComponent) {
    for (const TPair<int32, TArray<FGameBuffAppliedModifier>>& Pair : HandlesByInstance) {
        for (const FGameBuffAppliedModifier& Applied : Pair.Value) {
            InStatComponent.RemoveStatModifier(Applied.StatTag, Applied.Handle);
        }
    }
    Reset();
}

void FGameBuffModifierBridge::Reset() {
    HandlesByInstance.Reset();
}
