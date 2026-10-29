/**
 * @file FVersionedCache.h
 * @brief 三策略聚合缓存（Eager / Lazy / LRU）：不可变快照 + 索引节点池 + 引用计数兜底
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/ScopeRWLock.h"

#include "Cache/FCachePolicy.h"
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
     * @brief LRU 池节点（连续内存，Prev/Next 为池内索引链）
     */
    struct FLruNode {
        FName Key;
        int32 Prev = INDEX_NONE;
        int32 Next = INDEX_NONE;
    };

    /**
     * @brief 待析构旧状态（在锁内转移所有权，出锁后离开作用域自然析构，消除锁内析构延迟）
     */
    struct FRetiredStorage {
        TSharedPtr<const ValueMap> EagerMap;
        TSet<FName> ValidKeys;
        ValueMap Entries;
    };

public:
    /**
     * @brief 构造三策略缓存容器实例
     * @param Domain 缓存业务域名称（用于解析缓存策略）
     */
    explicit FVersionedCache(const FName& Domain);

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

private:
    int32 AcquireNode(FName Key) const;
    void LinkHead(int32 NodeIndex) const;
    void Unlink(int32 NodeIndex) const;
    void TouchLru(FName Key) const;
    void EvictIfNeeded() const;
    FRetiredStorage RetireAllStorage();

private:
    static ECacheStrategy ResolveEffectiveStrategy(const FCachePolicy& Policy, int32 RowCount);

private:
    FName Domain;
    FCachePolicy Policy;
    ECacheStrategy EffectiveStrategy = ECacheStrategy::Eager;
    TFunction<ValueType(FName Key)> BuildFn;

    uint32 Generation = 0;

    mutable FRWLock CacheLock;
    TSharedPtr<const ValueMap> EagerMap;

    mutable TSet<FName> ValidKeys;
    mutable ValueMap Entries;

    mutable TMap<FName, int32> LruIndex;
    mutable TArray<FLruNode> NodePool;
    mutable TArray<int32> FreeList;
    mutable int32 Head = INDEX_NONE;
    mutable int32 Tail = INDEX_NONE;
};

template<typename T>
FVersionedCache<T>::FVersionedCache(const FName& Domain)
    : Domain(Domain) {
    Policy = FCachePolicy::Resolve(Domain);
}

template<typename T>
void FVersionedCache<T>::Rebuild(const TArray<FName>& AllKeys, TFunction<ValueType(FName Key)> InBuildFn) {
    const FCachePolicy NewPolicy = FCachePolicy::Resolve(Domain);
    const ECacheStrategy NewStrategy = ResolveEffectiveStrategy(NewPolicy, AllKeys.Num());

    TSharedPtr<const ValueMap> BuiltSnapshot;
    TSet<FName> BuiltValidKeys;
    if (NewStrategy == ECacheStrategy::Eager) {
        ValueMap PlainMap;
        PlainMap.Reserve(AllKeys.Num());
        for (const FName& Key : AllKeys) {
            PlainMap.Add(Key, MakeShared<const ValueType>(InBuildFn(Key)));
        }
        BuiltSnapshot = MakeShared<const ValueMap>(MoveTemp(PlainMap));
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
            Entries.Reserve(FMath::Min(AllKeys.Num(), static_cast<int32>(NewPolicy.LruCapacity)));
            if (NewStrategy == ECacheStrategy::Lru) {
                NodePool.Reserve(static_cast<int32>(NewPolicy.LruCapacity));
                FreeList.Reserve(static_cast<int32>(NewPolicy.LruCapacity));
            }
        }
    }
}

template<typename T>
typename FVersionedCache<T>::ValueTypePtr FVersionedCache<T>::FindShared(FName Key) const {
    for (int32 Attempt = 0; Attempt < 2; ++Attempt) {
        TFunction<ValueType(FName Key)> LocalBuildFn;
        uint32 ObservedGeneration = 0;
        {
            FReadScopeLock ReadLock(CacheLock);
            ObservedGeneration = Generation;
            if (EagerMap.IsValid()) {
                const ValueTypePtr* Found = EagerMap->Find(Key);
                return Found != nullptr ? *Found : ValueTypePtr();
            }
            if (EffectiveStrategy != ECacheStrategy::Eager && !ValidKeys.Contains(Key)) {
                return ValueTypePtr();
            }
            if (EffectiveStrategy == ECacheStrategy::Lazy) {
                if (const ValueTypePtr* Found = Entries.Find(Key)) {
                    return *Found;
                }
            }
            if (EffectiveStrategy == ECacheStrategy::Lru) {
                if (const int32* NodeIndex = LruIndex.Find(Key)) {
                    if (Head == *NodeIndex) {
                        if (const ValueTypePtr* Found = Entries.Find(Key)) {
                            return *Found;
                        }
                    }
                }
            }
        }

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
                TouchLru(Key);
                return *Found;
            }
            if (!BuildFn) {
                return ValueTypePtr();
            }
            LocalBuildFn = BuildFn;
        }

        const ValueTypePtr Built = MakeShared<const ValueType>(LocalBuildFn(Key));

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
            TouchLru(Key);
            return *Found;
        }
        Entries.Add(Key, Built);
        if (EffectiveStrategy == ECacheStrategy::Lru) {
            const int32 NodeIndex = AcquireNode(Key);
            LruIndex.Add(Key, NodeIndex);
            LinkHead(NodeIndex);
            EvictIfNeeded();
        }
        return Built;
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
    FWriteScopeLock WriteLock(CacheLock);
    bool bRemoved = false;

    if (EffectiveStrategy == ECacheStrategy::Eager) {
        if (EagerMap.IsValid() && EagerMap->Contains(Key)) {
            ValueMap UpdatedMap = *EagerMap;
            UpdatedMap.Remove(Key);
            EagerMap = MakeShared<const ValueMap>(MoveTemp(UpdatedMap));
            bRemoved = true;
        }
    } else {
        if (ValidKeys.Remove(Key) > 0) {
            bRemoved = true;
        }
        if (Entries.Remove(Key) > 0) {
            bRemoved = true;
        }

        if (EffectiveStrategy == ECacheStrategy::Lru) {
            if (const int32* FoundIndex = LruIndex.Find(Key)) {
                const int32 NodeIndex = *FoundIndex;
                Unlink(NodeIndex);
                FreeList.Add(NodeIndex);
                LruIndex.Remove(Key);
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
int32 FVersionedCache<T>::AcquireNode(FName Key) const {
#if UE_VERSION_OLDER_THAN(5, 4, 0)
    const int32 NodeIndex = FreeList.Num() > 0 ? FreeList.Pop(false) : NodePool.AddDefaulted();
#else
    const int32 NodeIndex = FreeList.Num() > 0 ? FreeList.Pop(EAllowShrinking::No) : NodePool.AddDefaulted();
#endif
    NodePool[NodeIndex].Key = Key;
    return NodeIndex;
}

template<typename T>
void FVersionedCache<T>::LinkHead(int32 NodeIndex) const {
    FLruNode& Node = NodePool[NodeIndex];
    Node.Prev = INDEX_NONE;
    Node.Next = Head;
    if (Head != INDEX_NONE) {
        NodePool[Head].Prev = NodeIndex;
    }
    Head = NodeIndex;
    if (Tail == INDEX_NONE) {
        Tail = NodeIndex;
    }
}

template<typename T>
void FVersionedCache<T>::Unlink(int32 NodeIndex) const {
    FLruNode& Node = NodePool[NodeIndex];
    if (Node.Prev != INDEX_NONE) {
        NodePool[Node.Prev].Next = Node.Next;
    } else {
        Head = Node.Next;
    }
    if (Node.Next != INDEX_NONE) {
        NodePool[Node.Next].Prev = Node.Prev;
    } else {
        Tail = Node.Prev;
    }
    Node.Prev = INDEX_NONE;
    Node.Next = INDEX_NONE;
}

template<typename T>
void FVersionedCache<T>::TouchLru(FName Key) const {
    const int32* NodeIndex = LruIndex.Find(Key);
    if (NodeIndex == nullptr || Head == *NodeIndex) {
        return;
    }
    Unlink(*NodeIndex);
    LinkHead(*NodeIndex);
}

template<typename T>
void FVersionedCache<T>::EvictIfNeeded() const {
    while (static_cast<int64>(Entries.Num()) > Policy.LruCapacity && Tail != INDEX_NONE) {
        const int32 VictimIndex = Tail;
        const FName VictimKey = NodePool[VictimIndex].Key;
        Unlink(VictimIndex);
        LruIndex.Remove(VictimKey);
        Entries.Remove(VictimKey);
        FreeList.Add(VictimIndex);
    }
}

template<typename T>
typename FVersionedCache<T>::FRetiredStorage FVersionedCache<T>::RetireAllStorage() {
    FRetiredStorage Retired;
    Retired.EagerMap = MoveTemp(EagerMap);
    Retired.ValidKeys = MoveTemp(ValidKeys);
    Retired.Entries = MoveTemp(Entries);
    LruIndex.Reset();
    NodePool.Reset();
    FreeList.Reset();
    Head = INDEX_NONE;
    Tail = INDEX_NONE;
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
