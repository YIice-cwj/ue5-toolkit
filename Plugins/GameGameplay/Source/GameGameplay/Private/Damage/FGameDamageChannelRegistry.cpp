/**
 * @file FGameDamageChannelRegistry.cpp
 * @brief 运行时伤害通道绑定注册表实现
 */

#include "Damage/FGameDamageChannelRegistry.h"

#include "GameGameplayLog.h"
#include "Misc/ScopeRWLock.h"

namespace {

constexpr int32 InlineChannelSnapshotCapacity = 8;
constexpr int32 InlineDeduplicationTagCapacity = 16;

}  // namespace

/**
 * @brief 获取全局注册表单例
 * @return 注册表实例
 */
FGameDamageChannelRegistry& FGameDamageChannelRegistry::Get() {
    static FGameDamageChannelRegistry Registry;
    return Registry;
}

/**
 * @brief 注册（或覆盖）一条通道绑定
 * @param InExecutionClass 归属的 ExecCalc 类
 * @param InBinding 通道绑定
 * @return 参数合法且注册成功返回 true，否则返回 false
 */
bool FGameDamageChannelRegistry::Register(const UClass* InExecutionClass, const FDamageChannelBinding& InBinding) {
    if (InExecutionClass == nullptr || !InBinding.IsValid()) {
        GAME_GAS_LOG(LogGameDamage, Warning, TEXT("【伤害通道注册表】注册失败：ExecCalc 类、通道标签或目标属性无效。"));
        return false;
    }

    FRWScopeLock Lock(RWLock, SLT_Write);
    TArray<FDamageChannelBinding>& Bindings = BindingsByClass.FindOrAdd(TObjectKey<const UClass>(InExecutionClass));
    for (FDamageChannelBinding& Existing : Bindings) {
        if (Existing.ChannelTag == InBinding.ChannelTag) {
            if (Existing == InBinding) {
                return true;
            }
            Existing = InBinding;
            CachedFlattenedBindings.Reset();
            GAME_GAS_LOG(LogGameDamage, Log, TEXT("【伤害通道注册表】覆盖通道绑定: 类=[%s], 通道=[%s], 目标属性=[%s]。"),
                *InExecutionClass->GetName(), *InBinding.ChannelTag.ToString(), *InBinding.TargetAttribute.GetName());
            return true;
        }
    }

    Bindings.Add(InBinding);
    CachedFlattenedBindings.Reset();
    GAME_GAS_LOG(LogGameDamage, Log, TEXT("【伤害通道注册表】追加通道绑定: 类=[%s], 通道=[%s], 目标属性=[%s]。"),
        *InExecutionClass->GetName(), *InBinding.ChannelTag.ToString(), *InBinding.TargetAttribute.GetName());
    return true;
}

/**
 * @brief 注销指定类的单条通道绑定
 * @param InExecutionClass 归属的 ExecCalc 类
 * @param InChannelTag 通道标签
 * @return 成功注销返回 true
 */
bool FGameDamageChannelRegistry::Unregister(const UClass* InExecutionClass, const FGameplayTag& InChannelTag) {
    if (InExecutionClass == nullptr || !InChannelTag.IsValid()) {
        return false;
    }

    FRWScopeLock Lock(RWLock, SLT_Write);
    const TObjectKey<const UClass> Key(InExecutionClass);
    TArray<FDamageChannelBinding>* Bindings = BindingsByClass.Find(Key);
    if (Bindings == nullptr) {
        return false;
    }

    const int32 RemovedCount = Bindings->RemoveAll([&InChannelTag](const FDamageChannelBinding& Binding) {
        return Binding.ChannelTag == InChannelTag;
    });
    if (RemovedCount <= 0) {
        return false;
    }

    if (Bindings->IsEmpty()) {
        BindingsByClass.Remove(Key);
    }
    CachedFlattenedBindings.Reset();
    GAME_GAS_LOG(LogGameDamage, Log, TEXT("【伤害通道注册表】注销通道成功: 类=[%s], 通道=[%s], 移除数=%d。"),
        *InExecutionClass->GetName(), *InChannelTag.ToString(), RemovedCount);
    return true;
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
    TArray<FDamageChannelBinding> Removed;
    if (!BindingsByClass.RemoveAndCopyValue(TObjectKey<const UClass>(InExecutionClass), Removed)) {
        return 0;
    }

    CachedFlattenedBindings.Reset();
    if (!Removed.IsEmpty()) {
        GAME_GAS_LOG(LogGameDamage, Log, TEXT("【伤害通道注册表】注销类全部通道: 类=[%s], 移除数=%d。"),
            *InExecutionClass->GetName(), Removed.Num());
    }
    return Removed.Num();
}

/**
 * @brief 遍历指定类的继承合并绑定（锁内拷贝至栈内联快照、锁外执行闭包，防重入死锁）
 * @param InExecutionClass 目标 ExecCalc 类
 * @param InAction 对合并绑定切片执行的只读闭包
 */
void FGameDamageChannelRegistry::ForEachBinding(
    const UClass* InExecutionClass,
    TFunctionRef<void(TConstArrayView<FDamageChannelBinding>)> InAction
) const {
    if (InExecutionClass == nullptr) {
        return;
    }

    const TObjectKey<const UClass> Key(InExecutionClass);
    TArray<FDamageChannelBinding, TInlineAllocator<InlineChannelSnapshotCapacity>> LocalSnapshot;
    bool bCacheHit = false;
    {
        FRWScopeLock ReadLock(RWLock, SLT_ReadOnly);
        if (const TArray<FDamageChannelBinding>* Cached = CachedFlattenedBindings.Find(Key)) {
            LocalSnapshot.Append(*Cached);
            bCacheHit = true;
        }
    }

    if (!bCacheHit) {
        FRWScopeLock WriteLock(RWLock, SLT_Write);
        const TArray<FDamageChannelBinding>* Cached = CachedFlattenedBindings.Find(Key);
        if (Cached == nullptr) {
            TArray<FDamageChannelBinding>& NewCached = CachedFlattenedBindings.Add(Key);
            BuildFlattenedBindingsLocked(InExecutionClass, NewCached);
            Cached = &NewCached;
        }
        LocalSnapshot.Append(*Cached);
    }

    InAction(MakeArrayView(LocalSnapshot));
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
 * @brief 在已持写锁状态下沿类继承链构建扁平化通道列表（派生类同名通道优先）
 * @param InExecutionClass 目标 ExecCalc 类
 * @param OutFlattened 输出的扁平化绑定数组
 */
void FGameDamageChannelRegistry::BuildFlattenedBindingsLocked(
    const UClass* InExecutionClass,
    TArray<FDamageChannelBinding>& OutFlattened
) const {
    TArray<FGameplayTag, TInlineAllocator<InlineDeduplicationTagCapacity>> SeenTags;
    for (const UClass* CurrentClass = InExecutionClass; CurrentClass != nullptr; CurrentClass = CurrentClass->GetSuperClass()) {
        const TArray<FDamageChannelBinding>* Bindings = BindingsByClass.Find(TObjectKey<const UClass>(CurrentClass));
        if (Bindings == nullptr) {
            continue;
        }

        for (const FDamageChannelBinding& Binding : *Bindings) {
            if (!SeenTags.Contains(Binding.ChannelTag)) {
                OutFlattened.Add(Binding);
                SeenTags.Add(Binding.ChannelTag);
            }
        }
    }
}
