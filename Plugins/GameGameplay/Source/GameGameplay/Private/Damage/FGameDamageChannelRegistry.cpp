/**
 * @file FGameDamageChannelRegistry.cpp
 * @brief 运行时伤害通道绑定注册表实现
 */

#include "Damage/FGameDamageChannelRegistry.h"

#include "Misc/ScopeRWLock.h"
#include "Logging/GameLogCategories.h"

/**
 * @brief 注册（或覆盖）一条通道绑定
 * @param InExecutionClass 归属的 ExecCalc 类
 * @param InBinding 通道绑定
 */
void FGameDamageChannelRegistry::Register(const UClass* InExecutionClass, const FDamageChannelBinding& InBinding) {
    if (InExecutionClass == nullptr || !InBinding.ChannelTag.IsValid() || !InBinding.TargetAttribute.IsValid()) {
        GAME_LOG(Warning, TEXT("【伤害通道注册表】注册失败：类、通道标签或目标属性无效。"));
        return;
    }

    FRWScopeLock Lock(RWLock, SLT_Write);
    CachedFlattenedBindings.Reset();
    TArray<FDamageChannelBinding>& Bindings = BindingsByClass.FindOrAdd(TObjectKey<const UClass>(InExecutionClass));
    for (FDamageChannelBinding& Existing : Bindings) {
        if (Existing.ChannelTag.MatchesTagExact(InBinding.ChannelTag)) {
            Existing = InBinding;
            GAME_LOG(Log, TEXT("【伤害通道注册表】覆盖通道绑定: 类=[%s], 通道=[%s], 目标属性=[%s]。"),
                *InExecutionClass->GetName(), *InBinding.ChannelTag.ToString(), *InBinding.TargetAttribute.GetName());
            return;
        }
    }
    Bindings.Add(InBinding);
    GAME_LOG(Log, TEXT("【伤害通道注册表】追加通道绑定: 类=[%s], 通道=[%s], 目标属性=[%s]。"),
                *InExecutionClass->GetName(), *InBinding.ChannelTag.ToString(), *InBinding.TargetAttribute.GetName());
}

/**
 * @brief 注销指定类的单条通道绑定
 * @param InExecutionClass 归属的 ExecCalc 类
 * @param InChannelTag 通道标签
 * @return 是否注销成功
 */
bool FGameDamageChannelRegistry::Unregister(const UClass* InExecutionClass, const FGameplayTag& InChannelTag) {
    if (InExecutionClass == nullptr) {
        return false;
    }

    FRWScopeLock Lock(RWLock, SLT_Write);
    CachedFlattenedBindings.Reset();
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
    if (RemovedCount > 0) {
        GAME_LOG(Log, TEXT("【伤害通道注册表】注销通道成功: 类=[%s], 通道=[%s], 移除数=%d。"),
            *InExecutionClass->GetName(), *InChannelTag.ToString(), RemovedCount);
    }
    return RemovedCount > 0;
}

/**
 * @brief 注销指定类的全部通道绑定
 * @param InExecutionClass 归属的 ExecCalc 类
 * @return 被注销的绑定数量
 */
int32 FGameDamageChannelRegistry::UnregisterAll(const UClass* InExecutionClass) {
    if (InExecutionClass == nullptr) {
        return 0;
    }

    FRWScopeLock Lock(RWLock, SLT_Write);
    CachedFlattenedBindings.Reset();
    TArray<FDamageChannelBinding> Removed;
    if (!BindingsByClass.RemoveAndCopyValue(TObjectKey<const UClass>(InExecutionClass), Removed)) {
        return 0;
    }
    if (Removed.Num() > 0) {
        GAME_LOG(Log, TEXT("【伤害通道注册表】注销类全部通道: 类=[%s], 移除数=%d。"),
            *InExecutionClass->GetName(), Removed.Num());
    }
    return Removed.Num();
}

/**
 * @brief 零分配遍历指定类的合并绑定（在读锁内以只读切片执行，避免高频堆拷贝）
 * @param InExecutionClass 目标 ExecCalc 类
 * @param InAction 对合并绑定切片执行的只读闭包
 */
void FGameDamageChannelRegistry::ForEachBinding(const UClass* InExecutionClass, TFunctionRef<void(TConstArrayView<FDamageChannelBinding>)> InAction) const {
    if (InExecutionClass == nullptr) {
        return;
    }

    const TObjectKey<const UClass> Key(InExecutionClass);
    {
        FRWScopeLock ReadLock(RWLock, SLT_ReadOnly);
        if (const TArray<FDamageChannelBinding>* Cached = CachedFlattenedBindings.Find(Key)) {
            InAction(MakeArrayView(*Cached));
            return;
        }
    }

    FRWScopeLock WriteLock(RWLock, SLT_Write);
    if (const TArray<FDamageChannelBinding>* Cached = CachedFlattenedBindings.Find(Key)) {
        InAction(MakeArrayView(*Cached));
        return;
    }

    TArray<FDamageChannelBinding>& NewCached = CachedFlattenedBindings.Add(Key);
    TSet<FGameplayTag> AddedTags;
    for (const UClass* CurrentClass = InExecutionClass; CurrentClass != nullptr; CurrentClass = CurrentClass->GetSuperClass()) {
        const TArray<FDamageChannelBinding>* Bindings = BindingsByClass.Find(TObjectKey<const UClass>(CurrentClass));
        if (Bindings == nullptr) {
            continue;
        }

        for (const FDamageChannelBinding& Binding : *Bindings) {
            if (!AddedTags.Contains(Binding.ChannelTag)) {
                NewCached.Add(Binding);
                AddedTags.Add(Binding.ChannelTag);
            }
        }
    }
    InAction(MakeArrayView(NewCached));
}

/**
 * @brief 查询指定类直属的绑定数量（不含继承）
 * @param InExecutionClass 目标 ExecCalc 类
 * @return 绑定数量
 */
int32 FGameDamageChannelRegistry::Num(const UClass* InExecutionClass) const {
    if (InExecutionClass == nullptr) {
        return 0;
    }

    FRWScopeLock Lock(RWLock, SLT_ReadOnly);
    const TArray<FDamageChannelBinding>* Bindings = BindingsByClass.Find(TObjectKey<const UClass>(InExecutionClass));
    return Bindings != nullptr ? Bindings->Num() : 0;
}

/**
 * @brief 清空注册表（测试与热重载使用）
 */
void FGameDamageChannelRegistry::Reset() {
    FRWScopeLock Lock(RWLock, SLT_Write);
    CachedFlattenedBindings.Reset();
    BindingsByClass.Reset();
}

/**
 * @brief 获取全局注册表单例
 * @return 注册表实例
 */
FGameDamageChannelRegistry& FGameDamageChannelRegistry::Get() {
    static FGameDamageChannelRegistry Registry;
    return Registry;
}
