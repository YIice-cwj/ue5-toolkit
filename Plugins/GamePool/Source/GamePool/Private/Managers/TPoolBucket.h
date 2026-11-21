/**
 * @file TPoolBucket.h
 * @brief 对象池底层通用空闲与活跃实例存储桶模板（TDeque 保证 LIFO O(1) 热借出与 FIFO O(1) 冷淘汰）
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/Deque.h"
#include "Core/ObjectPoolTypes.h"
#include "UObject/ObjectPtr.h"

/**
 * @brief 单类别池化对象底层存储桶模板
 * @tparam TObjectType 池化对象类型（AActor 或 UObject）
 */
template <typename TObjectType>
struct TPoolBucket {
public:
    /**
     * @brief 空闲池条目：将对象强引用与归还时间戳绑定存储，彻底消除平行数组失步隐患
     */
    struct FIdleEntry {
        TObjectPtr<TObjectType> Instance;
        double ReturnTimeSeconds = 0.0;
    };

public:
    /**
     * @brief 获取当前活跃借出实例总数
     * @return 活跃实例数
     */
    [[nodiscard]] int32 GetActiveCount() const noexcept {
        return ActiveSet.Num();
    }

    /**
     * @brief 获取当前空闲实例总数
     * @return 空闲实例数
     */
    [[nodiscard]] int32 GetIdleCount() const noexcept {
        return IdleEntries.Num();
    }

    /**
     * @brief 从空闲队列尾部压入一个新归还条目（LIFO 热端）
     * @param InInstance 空闲对象指针
     * @param InReturnTimeSeconds 归还单调时间戳（秒）
     */
    void PushIdle(TObjectType* InInstance, double InReturnTimeSeconds) {
        IdleEntries.EmplaceLast(FIdleEntry{InInstance, InReturnTimeSeconds});
    }

    /**
     * @brief 从空闲队列尾部弹出最热空闲实例（O(1) LIFO 提升 CPU 缓存局部性）
     * @param OutEntry 传出空闲条目
     * @return 成功弹出返回 true，队列为空返回 false
     */
    [[nodiscard]] bool TryPopHotIdle(FIdleEntry& OutEntry) {
        return IdleEntries.TryPopLast(OutEntry);
    }

    /**
     * @brief 从空闲队列头部弹出最冷空闲实例（O(1) FIFO 零内存搬移）
     * @param OutEntry 传出空闲条目
     * @return 成功弹出返回 true，队列为空返回 false
     */
    [[nodiscard]] bool TryPopColdIdle(FIdleEntry& OutEntry) {
        return IdleEntries.TryPopFirst(OutEntry);
    }

    /**
     * @brief 按实例指针从空闲队列中移除指定条目（保持剩余条目时间戳单调序）
     * @param TargetInstance 待移除对象指针
     * @return 找到并移除返回 true
     */
    bool RemoveIdleInstance(const TObjectType* TargetInstance) {
        const int32 Count = IdleEntries.Num();
        for (int32 Index = 0; Index < Count; ++Index) {
            if (IdleEntries[Index].Instance.Get() != TargetInstance) {
                continue;
            }
            for (int32 ShiftIndex = Index + 1; ShiftIndex < Count; ++ShiftIndex) {
                IdleEntries[ShiftIndex - 1] = MoveTemp(IdleEntries[ShiftIndex]);
            }
            IdleEntries.PopLast();
            return true;
        }
        return false;
    }

    /**
     * @brief 向 UE 垃圾回收收集器注册本桶中全部空闲与活跃对象引用
     * @param Collector 引用收集器
     * @param ReferencingObject 引用宿主对象（用于 GC 引用链诊断）
     */
    void AddReferencedObjects(FReferenceCollector& Collector, const UObject* ReferencingObject = nullptr) {
        for (FIdleEntry& Entry : IdleEntries) {
            Collector.AddReferencedObject(Entry.Instance, ReferencingObject);
        }
        Collector.AddReferencedObjects(ActiveSet, ReferencingObject);
    }

public:
    TDeque<FIdleEntry> IdleEntries;
    TSet<TObjectPtr<TObjectType>> ActiveSet;
    int32 MaxIdleCount = 100;
    EPoolResetFlags ResetFlags = EPoolResetFlags::All;
    FPoolEvictionSettings EvictionSettings;
};
