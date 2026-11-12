/**
 * @file FVersionedCache.h
 * @brief 三策略聚合缓存（Eager / Lazy / LRU）：不可变快照 + 索引节点池 + 引用计数兜底
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/ScopeRWLock.h"

#include "Cache/FCachePolicy.h"
#include "Cache/FLruIndexPool.h"
#include "Config/FConfigRegistry.h"

/**
 * @brief 三策略缓存容器模板（模板类，声明与接口契约）
 * @tparam T 缓存值类型
 */
template<typename T>
class FVersionedCache {
public:
    using ValueType = T;
    using ValueTypePtr = TSharedPtr<const ValueType>;
    using ValueMap = TMap<FName, ValueTypePtr>;

private:
    /**
     * @brief 待析构旧状态（在锁内转移所有权，出锁后离开作用域自然析构，消除锁内析构延迟）
     */
    struct FRetiredStorage {
        TSharedPtr<ValueMap> EagerMap;
        TSet<FName> ValidKeys;
        ValueMap Entries;
    };

public:
    /**
     * @brief 构造三策略缓存容器实例
     * @param Domain 缓存业务域名称（用于解析缓存策略）
     */
    explicit FVersionedCache(const FName& Domain);

    ~FVersionedCache() = default;
    FVersionedCache(const FVersionedCache&) = delete;
    FVersionedCache& operator=(const FVersionedCache&) = delete;
    FVersionedCache(FVersionedCache&&) = delete;
    FVersionedCache& operator=(FVersionedCache&&) = delete;

    /**
     * @brief 预热重构入口：传入主表全部键与单条构建闭包
     * @param AllKeys 主表包含的全部键名列表
     * @param InBuildFn 单条条目构建函数（入参为条目键，返回构建后的数据实例）
     */
    void Rebuild(const TArray<FName>& AllKeys, TFunction<ValueType(FName Key)> InBuildFn);

    /**
     * @brief 安全持有查找（并发安全，返回共享指针以保证脱锁生命周期安全）
     * @param Key 目标缓存键
     * @return 缓存数据只读共享指针；若未命中且惰性构建失败则返回空指针
     */
    [[nodiscard]] ValueTypePtr FindShared(FName Key) const;

    /**
     * @brief 清空并重置缓存容器的所有内部状态与已缓存数据
     */
    void Reset();

    /**
     * @brief 精确失效单个键的缓存数据
     * @param Key 待失效的目标缓存键
     * @return 成功剔除缓存条目返回 true；若该键原本未缓存则返回 false
     */
    bool Invalidate(FName Key);

    /**
     * @brief 获取当前有效缓存条目数量
     * @return 活跃条目数
     */
    [[nodiscard]] int32 Num() const;

    /**
     * @brief 检查缓存容器是否为空
     * @return 为空返回 true
     */
    [[nodiscard]] bool IsEmpty() const;

    /**
     * @brief 检查指定键当前是否已存在于有效缓存中（不触发惰性构建）
     * @param Key 目标缓存键
     * @return 已缓存返回 true
     */
    [[nodiscard]] bool Contains(FName Key) const;

    /**
     * @brief 获取当前生效的缓存策略
     * @return 当前缓存策略枚举
     */
    [[nodiscard]] ECacheStrategy GetEffectiveStrategy() const;

    /**
     * @brief 获取当前缓存容器绑定的业务域名称
     * @return 域名称
     */
    [[nodiscard]] const FName& GetDomain() const noexcept {
        return Domain;
    }

    /**
     * @brief 获取当前内部重构代次（用于代际追踪与可观测性）
     * @return 递增代次序号
     */
    [[nodiscard]] uint64 GetGeneration() const noexcept;

private:
    FRetiredStorage RetireAllStorage();
    static ECacheStrategy ResolveEffectiveStrategy(const FCachePolicy& Policy, int32 RowCount);

private:
    FName Domain;
    FCachePolicy Policy;
    ECacheStrategy EffectiveStrategy = ECacheStrategy::Eager;
    TFunction<ValueType(FName Key)> BuildFn;

    uint64 Generation = 0;

    mutable FRWLock CacheLock;
    TSharedPtr<ValueMap> EagerMap;

    mutable TSet<FName> ValidKeys;
    mutable ValueMap Entries;
    mutable FLruIndexPool LruPool;
};

template<typename T>
FVersionedCache<T>::FVersionedCache(const FName& Domain)
    : Domain(Domain) {
    Policy = FCachePolicy::Resolve(Domain);
}

template<typename T>
void FVersionedCache<T>::Rebuild(const TArray<FName>& AllKeys, TFunction<ValueType(FName Key)> InBuildFn) {
    if (!InBuildFn) {
        Reset();
        return;
    }
    FCachePolicy NewPolicy = FCachePolicy::Resolve(Domain);
    const int32 ClampedCapacity = NewPolicy.GetClampedLruCapacity();
    NewPolicy.LruCapacity = ClampedCapacity;
    const ECacheStrategy NewStrategy = ResolveEffectiveStrategy(NewPolicy, AllKeys.Num());

    TSharedPtr<ValueMap> BuiltSnapshot;
    TSet<FName> BuiltValidKeys;
    if (NewStrategy == ECacheStrategy::Eager) {
        ValueMap PlainMap;
        PlainMap.Reserve(AllKeys.Num());
        for (const FName& Key : AllKeys) {
            PlainMap.Add(Key, MakeShared<const ValueType>(InBuildFn(Key)));
        }
        BuiltSnapshot = MakeShared<ValueMap>(MoveTemp(PlainMap));
    } else {
        BuiltValidKeys.Reserve(AllKeys.Num());
        for (const FName& Key : AllKeys) {
            BuiltValidKeys.Add(Key);
        }
    }

    FRetiredStorage Retired;
    {
        FWriteScopeLock WriteLock(CacheLock);
        Retired = RetireAllStorage();
        ++Generation;
        Policy = NewPolicy;
        BuildFn = MoveTemp(InBuildFn);
        EffectiveStrategy = NewStrategy;
        if (NewStrategy == ECacheStrategy::Eager) {
            EagerMap = BuiltSnapshot;
        } else {
            ValidKeys = MoveTemp(BuiltValidKeys);
            Entries.Reserve(FMath::Min(AllKeys.Num(), ClampedCapacity));
            if (NewStrategy == ECacheStrategy::Lru) {
                LruPool.Reserve(ClampedCapacity);
            }
        }
    }
}

template<typename T>
typename FVersionedCache<T>::ValueTypePtr FVersionedCache<T>::FindShared(FName Key) const {
    if (Key.IsNone()) {
        return ValueTypePtr();
    }
    for (int32 Attempt = 0; Attempt < 2; ++Attempt) {
        TFunction<ValueType(FName Key)> LocalBuildFn;
        uint64 ObservedGeneration = 0;
        {
            FReadScopeLock ReadLock(CacheLock);
            ObservedGeneration = Generation;
            if (EffectiveStrategy == ECacheStrategy::Eager) {
                if (EagerMap.IsValid()) {
                    const ValueTypePtr* Found = EagerMap->Find(Key);
                    return Found != nullptr ? *Found : ValueTypePtr();
                }
                return ValueTypePtr();
            }
            if (!ValidKeys.Contains(Key)) {
                return ValueTypePtr();
            }
            if (const ValueTypePtr* Found = Entries.Find(Key)) {
                if (EffectiveStrategy == ECacheStrategy::Lru) {
                    LruPool.MarkReferenced(Key);
                }
                return *Found;
            }
            if (!BuildFn) {
                return ValueTypePtr();
            }
            LocalBuildFn = BuildFn;
        }

        const ValueTypePtr Built = MakeShared<const ValueType>(LocalBuildFn(Key));

        {
            FWriteScopeLock WriteLock(CacheLock);
            if (Generation != ObservedGeneration) {
                continue;
            }
            if (EffectiveStrategy == ECacheStrategy::Eager) {
                const ValueTypePtr* Found = EagerMap.IsValid() ? EagerMap->Find(Key) : nullptr;
                return Found != nullptr ? *Found : ValueTypePtr();
            }
            if (!ValidKeys.Contains(Key)) {
                return ValueTypePtr();
            }
            if (const ValueTypePtr* Found = Entries.Find(Key)) {
                if (EffectiveStrategy == ECacheStrategy::Lru) {
                    LruPool.Touch(Key);
                }
                return *Found;
            }
            Entries.Add(Key, Built);
            if (EffectiveStrategy == ECacheStrategy::Lru) {
                const int32 SafeCapacity = Policy.GetClampedLruCapacity();
                const int32 NodeIndex = LruPool.AllocateNode(Key);
                LruPool.InsertAtHead(NodeIndex);
                while (Entries.Num() > SafeCapacity) {
                    const FName VictimKey = LruPool.EvictTail();
                    if (VictimKey.IsNone()) {
                        break;
                    }
                    Entries.Remove(VictimKey);
                }
            }
            return Built;
        }
    }

    return ValueTypePtr();
}

template<typename T>
void FVersionedCache<T>::Reset() {
    FRetiredStorage Retired;
    {
        FWriteScopeLock WriteLock(CacheLock);
        Retired = RetireAllStorage();
        ++Generation;
    }
}

template<typename T>
bool FVersionedCache<T>::Invalidate(FName Key) {
    if (Key.IsNone()) {
        return false;
    }
    ValueTypePtr RetiredValue;
    bool bRemoved = false;
    {
        FWriteScopeLock WriteLock(CacheLock);
        if (EffectiveStrategy == ECacheStrategy::Eager) {
            if (EagerMap.IsValid() && EagerMap->RemoveAndCopyValue(Key, RetiredValue)) {
                bRemoved = true;
            }
        } else {
            if (ValidKeys.Remove(Key) > 0) {
                bRemoved = true;
            }
            if (Entries.RemoveAndCopyValue(Key, RetiredValue)) {
                bRemoved = true;
            }

            if (EffectiveStrategy == ECacheStrategy::Lru) {
                LruPool.Remove(Key);
            }
        }
    }

    return bRemoved;
}

template<typename T>
int32 FVersionedCache<T>::Num() const {
    FReadScopeLock ReadLock(CacheLock);
    if (EffectiveStrategy == ECacheStrategy::Eager) {
        return EagerMap.IsValid() ? EagerMap->Num() : 0;
    }
    return Entries.Num();
}

template<typename T>
bool FVersionedCache<T>::IsEmpty() const {
    FReadScopeLock ReadLock(CacheLock);
    if (EffectiveStrategy == ECacheStrategy::Eager) {
        return !EagerMap.IsValid() || EagerMap->IsEmpty();
    }
    return Entries.IsEmpty();
}

template<typename T>
bool FVersionedCache<T>::Contains(FName Key) const {
    if (Key.IsNone()) {
        return false;
    }
    FReadScopeLock ReadLock(CacheLock);
    if (EffectiveStrategy == ECacheStrategy::Eager) {
        return EagerMap.IsValid() && EagerMap->Contains(Key);
    }
    return Entries.Contains(Key);
}

template<typename T>
ECacheStrategy FVersionedCache<T>::GetEffectiveStrategy() const {
    FReadScopeLock ReadLock(CacheLock);
    return EffectiveStrategy;
}

template<typename T>
uint64 FVersionedCache<T>::GetGeneration() const noexcept {
    FReadScopeLock ReadLock(CacheLock);
    return Generation;
}

template<typename T>
typename FVersionedCache<T>::FRetiredStorage FVersionedCache<T>::RetireAllStorage() {
    FRetiredStorage Retired;
    Retired.EagerMap = MoveTemp(EagerMap);
    Retired.ValidKeys = MoveTemp(ValidKeys);
    Retired.Entries = MoveTemp(Entries);
    LruPool.Reset();
    BuildFn.Reset();
    EffectiveStrategy = ECacheStrategy::Eager;
    return Retired;
}

template<typename T>
ECacheStrategy FVersionedCache<T>::ResolveEffectiveStrategy(const FCachePolicy& Policy, int32 RowCount) {
    if (Policy.Strategy != ECacheStrategy::Automatic) {
        return Policy.Strategy;
    }
    const int64 RowCount64 = static_cast<int64>(RowCount);
    if (RowCount64 >= Policy.LruMinRows) {
        return ECacheStrategy::Lru;
    }
    if (RowCount64 >= Policy.LazyMinRows) {
        return ECacheStrategy::Lazy;
    }
    return ECacheStrategy::Eager;
}
