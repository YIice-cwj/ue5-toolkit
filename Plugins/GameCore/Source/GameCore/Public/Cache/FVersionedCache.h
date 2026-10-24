/**
 * @file FVersionedCache.h
 * @brief 三策略聚合缓存（Eager / Lazy / LRU）：不可变快照 + 索引节点池 + 引用计数兜底
 *
 * 设计思路：
 * - 指针稳定化：值统一以 TSharedPtr<const ValueType> 持有，容器扩容搬移不改变对象地址；
 *   驱逐与 Rebuild 期间由引用计数兜底，已持有 FindShared 的调用方绝不悬垂；
 * - Eager 档：不可变映射 TSharedPtr<const TMap<...>>，构建在锁外完成后原子换新，读路径仅瞬态读锁；
 * - LRU 档：连续节点池 TArray<FLruNode> + int32 索引链 + 空闲槽回收，取代 TDoubleLinkedList 与
 *   Node* 裸指针映射——O(1) 触达与驱逐、零每 key 堆分配，RemoveNode 参数类 UAF 随结构消亡；
 * - 策略自适应：FCachePolicy::Resolve(Domain) 从 FConfigRegistry 读取策略与阈值，Rebuild 按主表行数落定；
 * - 并发：内部 FRWLock 保护惰性填充与 LRU 簿记，簿记不逃逸锁；临界区内只做指针交换，
 *   旧快照/旧条目经 RetireAllStorage 腾挪到局部对象、出锁后再析构（锁内不做耗时操作）；
 * - Const 正确性：查询接口为 const（逻辑只读），惰性填充经 mutable 成员完成。
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/ScopeRWLock.h"

#include "Config/FConfigRegistry.h"

/**
 * @brief 缓存策略枚举
 */
enum class ECacheStrategy : uint8 {
    /** 按表体量阈值自动判定 */
    Automatic,
    /** 全量预热 */
    Eager,
    /** 惰性构建 */
    Lazy,
    /** LRU 限长淘汰 */
    Lru,
};

/**
 * @brief 缓存策略配置（从 FConfigRegistry 按命名约定键解析）
 */
struct GAMECORE_API FCachePolicy {
public:
    /**
     * @brief 从 FConfigRegistry 按命名约定键解析域策略
     * @param Domain 域名称（键前缀 Cache.{Domain}.）
     * @return 解析后的策略（未注册键回退默认）
     */
    static FCachePolicy Resolve(const FName& Domain);

public:
    /** 生效策略（Automatic 时由 Rebuild 按行数判定） */
    ECacheStrategy Strategy = ECacheStrategy::Automatic;
    /** auto 判定：行数 >= 该值 → lazy */
    int64 LazyMinRows = MAX_int64;
    /** auto 判定：行数 >= 该值 → lru（优先于 lazy） */
    int64 LruMinRows = MAX_int64;
    /** lru 容量上限（超出淘汰最久未用） */
    int64 LruCapacity = 10000;
};

/**
 * @brief 三策略缓存容器模板（模板类，无导出宏，实现定义于本头文件类外）
 * @tparam T 缓存值类型
 */
template<typename T>
class FVersionedCache {
public:
    using ValueType = T;
    using ValueTypePtr = TSharedPtr<const ValueType>;
    using ValueMap = TMap<FName, ValueTypePtr>;

private:
    /** LRU 池节点：连续内存，prev/next 为池索引链 */
    struct FLruNode {
        FName Key;
        int32 Prev = INDEX_NONE;
        int32 Next = INDEX_NONE;
    };

    /** 待析构旧状态：锁内腾挪、出锁后自然析构，严禁在临界区内释放大对象 */
    struct FRetiredStorage {
        TSharedPtr<const ValueMap> EagerMap;
        TSet<FName> ValidKeys;
        ValueMap Entries;
    };

public:
    /**
     * @brief 构造（按域名解析策略）
     * @param Domain 域名称
     */
    explicit FVersionedCache(const FName& Domain);

    /**
     * @brief 预热入口：传入主表全部键与单条构建函数（闭包仅此一次传入并保存）
     * @param AllKeys 主表全部键
     * @param InBuildFn 单条构建函数（含外键校验；惰性填充路径下于锁外乐观调用）
     */
    void Rebuild(const TArray<FName>& AllKeys, TFunction<ValueType(FName Key)> InBuildFn);

    /**
     * @brief 安全持有查找（任意策略可用；驱逐、Rebuild 与 Reset 期间由引用计数兜底）
     * @param Key 缓存键
     * @return 缓存值共享指针；未命中返回空
     *
     * 实现思路：Eager 档与 Lazy 命中均在读锁内直接完成查表，读者互不阻塞；
     * LRU 命中且节点已是链表头时同样在读锁内直接出值，避免无谓写锁重排；
     * 未命中或需重排时于锁外乐观调用 BuildFn 构建值，再经瞬态写锁换入
     * （杜绝锁内调用用户代码与死锁隐患；写锁内二次判定策略与既有键防竞态重复插入）。
     */
    [[nodiscard]] ValueTypePtr FindShared(FName Key) const;

    /**
     * @brief 清空缓存（Eager 快照与 LRU 簿记全清；已由 FindShared 持有的值由引用计数兜底，不受影响）
     */
    void Reset();

    /**
     * @brief 精确失效单个键的缓存（单条淘汰，无需整表 Rebuild）
     * @param Key 目标键
     * @return 若命中并成功剔除返回 true，未缓存返回 false
     */
    bool Invalidate(FName Key);

    /**
     * @brief 当前条目数
     * @return 条目数
     */
    [[nodiscard]] int32 Num() const;

private:
    int32 AcquireNode(FName Key) const;

    void LinkHead(int32 NodeIndex) const;

    void Unlink(int32 NodeIndex) const;

    void TouchLru(FName Key) const;

    void EvictIfNeeded() const;

    /**
     * @brief 腾挪并清空全部存储与策略（调用方必须已持写锁）
     * @return 待析构旧状态，由调用方在出锁后释放
     */
    FRetiredStorage RetireAllStorage();

private:
    static ECacheStrategy ResolveEffectiveStrategy(const FCachePolicy& Policy, int32 RowCount);

private:
    FName Domain;
    FCachePolicy Policy;
    ECacheStrategy EffectiveStrategy = ECacheStrategy::Eager;
    TFunction<ValueType(FName Key)> BuildFn;

    /** 存储代际号（Rebuild / Reset 自增；惰性构建窗口内代际变化即丢弃旧构建结果并重试） */
    uint32 Generation = 0;

    mutable FRWLock CacheLock;
    TSharedPtr<const ValueMap> EagerMap;
    TSet<FName> ValidKeys;
    mutable ValueMap Entries;
    mutable TMap<FName, int32> LruIndex;
    mutable TArray<FLruNode> NodePool;
    mutable TArray<int32> FreeList;
    mutable int32 Head = INDEX_NONE;
    mutable int32 Tail = INDEX_NONE;
};

// ---------------------------------------------------------------------------
// 模板实现（定义全部置于类外，随头文件对使用方可见）
// ---------------------------------------------------------------------------

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
