/**
 * @file FGameDamageChannelRegistry.cpp
 * @brief 运行时伤害通道绑定注册表实现
 */

#include "AbilitySystem/FGameDamageChannelRegistry.h"

#include "Misc/ScopeRWLock.h"

FGameDamageChannelRegistry& FGameDamageChannelRegistry::Get() {
    static FGameDamageChannelRegistry Registry;
    return Registry;
}

void FGameDamageChannelRegistry::Register(const UClass* InExecutionClass, const FDamageChannelBinding& InBinding) {
    if (InExecutionClass == nullptr || !InBinding.ChannelTag.IsValid() || !InBinding.TargetAttribute.IsValid()) {
        return;
    }

    FRWScopeLock Lock(RWLock, SLT_Write);
    TArray<FDamageChannelBinding>& Bindings = BindingsByClass.FindOrAdd(TObjectKey<const UClass>(InExecutionClass));
    for (FDamageChannelBinding& Existing : Bindings) {
        if (Existing.ChannelTag.MatchesTagExact(InBinding.ChannelTag)) {
            Existing = InBinding;
            return;
        }
    }
    Bindings.Add(InBinding);
}

bool FGameDamageChannelRegistry::Unregister(const UClass* InExecutionClass, const FGameplayTag& InChannelTag) {
    if (InExecutionClass == nullptr) {
        return false;
    }

    FRWScopeLock Lock(RWLock, SLT_Write);
    const TObjectKey<const UClass> Key(InExecutionClass);
    TArray<FDamageChannelBinding>* Bindings = BindingsByClass.Find(Key);
    if (Bindings == nullptr) {
        return false;
    }

    const int32 RemovedCount = Bindings->RemoveAll([&InChannelTag](const FDamageChannelBinding& Binding) {
        return Binding.ChannelTag.MatchesTagExact(InChannelTag);
    });
    if (Bindings->IsEmpty()) {
        BindingsByClass.Remove(Key);
    }
    return RemovedCount > 0;
}

int32 FGameDamageChannelRegistry::UnregisterAll(const UClass* InExecutionClass) {
    if (InExecutionClass == nullptr) {
        return 0;
    }

    FRWScopeLock Lock(RWLock, SLT_Write);
    TArray<FDamageChannelBinding> Removed;
    if (!BindingsByClass.RemoveAndCopyValue(TObjectKey<const UClass>(InExecutionClass), Removed)) {
        return 0;
    }
    return Removed.Num();
}

void FGameDamageChannelRegistry::GetBindings(const UClass* InExecutionClass, TArray<FDamageChannelBinding>& OutBindings) const {
    OutBindings.Reset();
    if (InExecutionClass == nullptr) {
        return;
    }

    FRWScopeLock Lock(RWLock, SLT_ReadOnly);
    TSet<FGameplayTag> AddedTags;
    for (const UClass* CurrentClass = InExecutionClass; CurrentClass != nullptr; CurrentClass = CurrentClass->GetSuperClass()) {
        const TArray<FDamageChannelBinding>* Bindings = BindingsByClass.Find(TObjectKey<const UClass>(CurrentClass));
        if (Bindings == nullptr) {
            continue;
        }

        for (const FDamageChannelBinding& Binding : *Bindings) {
            if (!AddedTags.Contains(Binding.ChannelTag)) {
                OutBindings.Add(Binding);
                AddedTags.Add(Binding.ChannelTag);
            }
        }
    }
}

int32 FGameDamageChannelRegistry::Num(const UClass* InExecutionClass) const {
    if (InExecutionClass == nullptr) {
        return 0;
    }

    FRWScopeLock Lock(RWLock, SLT_ReadOnly);
    const TArray<FDamageChannelBinding>* Bindings = BindingsByClass.Find(TObjectKey<const UClass>(InExecutionClass));
    return Bindings != nullptr ? Bindings->Num() : 0;
}

void FGameDamageChannelRegistry::Reset() {
    FRWScopeLock Lock(RWLock, SLT_Write);
    BindingsByClass.Reset();
}
