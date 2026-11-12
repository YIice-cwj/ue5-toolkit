/**
 * @file FLruIndexPool.h
 * @brief 连续内存 LRU 索引链池（单一职责 · 负责 O(1) 触达、驱逐与空闲槽回收 · 零每键堆分配）
 */

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformAtomics.h"

/**
 * @brief 基于连续数组的高性能 LRU 索引链管理器（融合 Clock 二次机会原子热度标记，支持并发读锁下零写锁竞争记录访问热度）
 */
struct FLruIndexPool {
public:
    /**
     * @brief LRU 索引池节点
     */
    struct FLruNode {
        /**
         * @brief 缓存键名称
         */
        FName Key;

        /**
         * @brief 前驱节点在 Pool 数组中的索引（INDEX_NONE 表示链表头）
         */
        int32 Prev = INDEX_NONE;

        /**
         * @brief 后继节点在 Pool 数组中的索引（INDEX_NONE 表示链表尾）
         */
        int32 Next = INDEX_NONE;

        /**
         * @brief 并发读命中原子二次机会热度位（1 表示近期被读命中，淘汰扫描时晋升至链表头并清零；32 位整型全平台通用且自然对齐）
         */
        mutable volatile int32 HotBit = 0;
    };

    /**
     * @brief 预分配的节点连续存储池
     */
    TArray<FLruNode> Pool;

    /**
     * @brief 缓存键到池节点索引的快速映射表
     */
    TMap<FName, int32> KeyToNode;

    /**
     * @brief 最近访问的头部节点索引
     */
    int32 Head = INDEX_NONE;

    /**
     * @brief 最久未访问的尾部节点索引（淘汰候选）
     */
    int32 Tail = INDEX_NONE;

    /**
     * @brief 空闲节点回收链表的头索引
     */
    int32 FreeHead = INDEX_NONE;

    /**
     * @brief 重置并清空索引池状态与所有节点链表
     */
    void Reset() {
        Pool.Reset();
        KeyToNode.Reset();
        Head = INDEX_NONE;
        Tail = INDEX_NONE;
        FreeHead = INDEX_NONE;
    }

    /**
     * @brief 预分配节点池与哈希表容量
     * @param Capacity 预期容量上限
     */
    void Reserve(int32 Capacity) {
        Pool.Reserve(Capacity);
        KeyToNode.Reserve(Capacity);
    }

    /**
     * @brief 检查指定键是否存在于 LRU 索引中
     * @param Key 目标缓存键
     * @return 存在返回 true
     */
    [[nodiscard]] bool Contains(FName Key) const {
        return KeyToNode.Contains(Key);
    }

    /**
     * @brief 检查指定键是否处于活跃链表头部（最热条目）
     * @param Key 目标缓存键
     * @return 处于头部返回 true
     */
    [[nodiscard]] bool IsHead(FName Key) const {
        if (const int32* FoundIdx = KeyToNode.Find(Key)) {
            return Head == *FoundIdx;
        }
        return false;
    }

    /**
     * @brief 获取当前处于活跃双向链表头部的键名（最热条目）
     * @return 头部条目键名；若链表为空则返回 NAME_None
     */
    [[nodiscard]] FName GetHeadKey() const noexcept {
        return Head != INDEX_NONE ? Pool[Head].Key : NAME_None;
    }

    /**
     * @brief 获取当前处于活跃双向链表尾部的键名（最冷淘汰候选条目）
     * @return 尾部条目键名；若链表为空则返回 NAME_None
     */
    [[nodiscard]] FName GetTailKey() const noexcept {
        return Tail != INDEX_NONE ? Pool[Tail].Key : NAME_None;
    }

    /**
     * @brief 获取节点存储池当前容量
     * @return 节点池当前已分配容量
     */
    [[nodiscard]] int32 GetCapacity() const noexcept {
        return Pool.Max();
    }

    /**
     * @brief 获取当前已索引的活跃节点条目总数
     * @return 活跃条目数量
     */
    [[nodiscard]] int32 Num() const noexcept {
        return KeyToNode.Num();
    }

    /**
     * @brief 检查 LRU 索引池是否为空
     * @return 为空返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return KeyToNode.IsEmpty();
    }

    /**
     * @brief 在并发读锁内原子标记指定键被命中访问（零写锁竞争，供淘汰时执行 Clock 二次机会晋升）
     * @param Key 被命中的缓存键
     * @return 键存在于池中并完成标记返回 true
     */
    bool MarkReferenced(FName Key) const {
        if (const int32* FoundIdx = KeyToNode.Find(Key)) {
            FPlatformAtomics::InterlockedExchange(&Pool[*FoundIdx].HotBit, 1);
            return true;
        }
        return false;
    }

    /**
     * @brief 刷新指定键的访问热度（将其移至双向链表头部并标记热度位）
     * @param Key 被访问的缓存键
     */
    void Touch(FName Key) {
        const int32* FoundIdx = KeyToNode.Find(Key);
        if (FoundIdx == nullptr) {
            return;
        }
        const int32 NodeIdx = *FoundIdx;
        FPlatformAtomics::InterlockedExchange(&Pool[NodeIdx].HotBit, 1);
        if (Head == NodeIdx) {
            return;
        }

        RemoveFromChain(NodeIdx);
        InsertAtHead(NodeIdx);
    }

    /**
     * @brief 从索引池与双向链表中移除指定键并回收节点槽位
     * @param Key 待移除的缓存键
     * @return 成功移除返回 true，键不存在返回 false
     */
    bool Remove(FName Key) {
        if (const int32* FoundIdx = KeyToNode.Find(Key)) {
            const int32 NodeIdx = *FoundIdx;
            RemoveFromChain(NodeIdx);
            KeyToNode.Remove(Key);
            Pool[NodeIdx].Key = NAME_None;
            Pool[NodeIdx].Prev = INDEX_NONE;
            Pool[NodeIdx].Next = FreeHead;
            FPlatformAtomics::InterlockedExchange(&Pool[NodeIdx].HotBit, 0);
            FreeHead = NodeIdx;
            return true;
        }
        return false;
    }

    /**
     * @brief 将指定索引的节点从活跃双向链表中剥离
     * @param NodeIdx 待剥离的节点池索引
     */
    void RemoveFromChain(int32 NodeIdx) {
        FLruNode& Node = Pool[NodeIdx];
        if (Node.Prev != INDEX_NONE) {
            Pool[Node.Prev].Next = Node.Next;
        } else {
            Head = Node.Next;
        }

        if (Node.Next != INDEX_NONE) {
            Pool[Node.Next].Prev = Node.Prev;
        } else {
            Tail = Node.Prev;
        }

        Node.Prev = INDEX_NONE;
        Node.Next = INDEX_NONE;
    }

    /**
     * @brief 将指定索引的节点插入到活跃双向链表头部
     * @param NodeIdx 待插入的节点池索引
     */
    void InsertAtHead(int32 NodeIdx) {
        FLruNode& Node = Pool[NodeIdx];
        Node.Prev = INDEX_NONE;
        Node.Next = Head;

        if (Head != INDEX_NONE) {
            Pool[Head].Prev = NodeIdx;
        }
        Head = NodeIdx;

        if (Tail == INDEX_NONE) {
            Tail = NodeIdx;
        }
    }

    /**
     * @brief 为新键分配节点槽位（优先复用既有槽位或空闲链表槽位，否则扩容数组）
     * @param Key 新增条目的缓存键
     * @return 分配到的节点在 Pool 数组中的索引
     */
    int32 AllocateNode(FName Key) {
        if (const int32* FoundIdx = KeyToNode.Find(Key)) {
            const int32 ExistingIdx = *FoundIdx;
            RemoveFromChain(ExistingIdx);
            Pool[ExistingIdx] = FLruNode{Key, INDEX_NONE, INDEX_NONE, 0};
            return ExistingIdx;
        }

        int32 NodeIdx = INDEX_NONE;
        if (FreeHead != INDEX_NONE) {
            NodeIdx = FreeHead;
            FreeHead = Pool[FreeHead].Next;
            Pool[NodeIdx] = FLruNode{Key, INDEX_NONE, INDEX_NONE, 0};
        } else {
            NodeIdx = Pool.Num();
            Pool.Emplace(FLruNode{Key, INDEX_NONE, INDEX_NONE, 0});
        }
        KeyToNode.Add(Key, NodeIdx);
        return NodeIdx;
    }

    /**
     * @brief 淘汰并回收尾部最久未访问的节点（若尾部节点持有并发读命中的二次机会热度位，则清零热度位并晋升至链表头继续扫描）
     * @return 被淘汰节点的键名；若链表为空则返回 NAME_None
     */
    FName EvictTail() {
        if (Tail == INDEX_NONE) {
            return NAME_None;
        }

        const int32 MaxScanSteps = KeyToNode.Num();
        for (int32 Step = 0; Step < MaxScanSteps && Tail != INDEX_NONE; ++Step) {
            const int32 CandidateIdx = Tail;
            if (FPlatformAtomics::InterlockedExchange(&Pool[CandidateIdx].HotBit, 0) != 0) {
                if (Head != CandidateIdx) {
                    RemoveFromChain(CandidateIdx);
                    InsertAtHead(CandidateIdx);
                }
            } else {
                break;
            }
        }

        if (Tail == INDEX_NONE) {
            return NAME_None;
        }

        const int32 EvictedIdx = Tail;
        const FName EvictedKey = Pool[EvictedIdx].Key;

        RemoveFromChain(EvictedIdx);
        KeyToNode.Remove(EvictedKey);

        Pool[EvictedIdx].Key = NAME_None;
        Pool[EvictedIdx].Prev = INDEX_NONE;
        Pool[EvictedIdx].Next = FreeHead;
        FPlatformAtomics::InterlockedExchange(&Pool[EvictedIdx].HotBit, 0);
        FreeHead = EvictedIdx;

        return EvictedKey;
    }
};
