/**
 * @file FLruIndexPool.h
 * @brief 连续内存 LRU 索引链池（单一职责 · 负责 O(1) 触达、驱逐与空闲槽回收 · 零每键堆分配）
 */

#pragma once

#include "CoreMinimal.h"

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
};

/**
 * @brief 基于连续数组的高性能 LRU 索引链管理器
 */
struct FLruIndexPool {
public:
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
     * @brief 刷新指定键的访问热度（将其移至双向链表头部）
     * @param Key 被访问的缓存键
     */
    void Touch(FName Key) {
        const int32* FoundIdx = KeyToNode.Find(Key);
        if (FoundIdx == nullptr) {
            return;
        }
        const int32 NodeIdx = *FoundIdx;
        if (Head == NodeIdx) {
            return;
        }

        RemoveFromChain(NodeIdx);
        InsertAtHead(NodeIdx);
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
     * @brief 为新键分配节点槽位（优先复用空闲链表槽位，否则扩容数组）
     * @param Key 新增条目的缓存键
     * @return 分配到的节点在 Pool 数组中的索引
     */
    int32 AllocateNode(FName Key) {
        int32 NodeIdx = INDEX_NONE;
        if (FreeHead != INDEX_NONE) {
            NodeIdx = FreeHead;
            FreeHead = Pool[FreeHead].Next;
            Pool[NodeIdx] = FLruNode{Key, INDEX_NONE, INDEX_NONE};
        } else {
            NodeIdx = Pool.Num();
            Pool.Emplace(FLruNode{Key, INDEX_NONE, INDEX_NONE});
        }
        KeyToNode.Add(Key, NodeIdx);
        return NodeIdx;
    }

    /**
     * @brief 淘汰并回收尾部最久未访问的节点
     * @return 被淘汰节点的键名；若链表为空则返回 NAME_None
     */
    FName EvictTail() {
        if (Tail == INDEX_NONE) {
            return NAME_None;
        }

        const int32 EvictedIdx = Tail;
        const FName EvictedKey = Pool[EvictedIdx].Key;

        RemoveFromChain(EvictedIdx);
        KeyToNode.Remove(EvictedKey);

        Pool[EvictedIdx].Key = NAME_None;
        Pool[EvictedIdx].Next = FreeHead;
        FreeHead = EvictedIdx;

        return EvictedKey;
    }
};
