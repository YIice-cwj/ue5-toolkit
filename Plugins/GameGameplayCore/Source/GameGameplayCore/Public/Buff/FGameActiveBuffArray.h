/**
 * @file FGameActiveBuffArray.h
 * @brief 活动 Buff 的增量复制容器（单一职责 · 负责 FastArray 增量网络序列化与差分派发）
 */

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Buff/FGameBuffInstance.h"

#include "FGameActiveBuffArray.generated.h"

class UGameBuffComponent;

/**
 * @brief 活动 Buff 的增量复制容器（仅同步变更条目与删除索引）
 */
USTRUCT()
struct GAMEGAMEPLAYCORE_API FGameActiveBuffArray : public FFastArraySerializer {
    GENERATED_BODY()

    UPROPERTY()
    TArray<FGameBuffInstance> Items;

    UPROPERTY(NotReplicated)
    TWeakObjectPtr<UGameBuffComponent> OwnerComponent = nullptr;

    /**
     * @brief 增量复制入口：委托引擎 FastArrayDeltaSerialize 同步变更条目与删除索引
     * @param DeltaParms 增量序列化参数
     * @return 是否成功序列化
     */
    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms) {
        return FFastArraySerializer::FastArrayDeltaSerialize<FGameBuffInstance, FGameActiveBuffArray>(Items, DeltaParms, *this);
    }

    /**
     * @brief 客户端收到新增条目后：批量挂接修正器、刷新 Tick 状态并广播差异
     * @param AddedIndices 新增条目下标
     * @param FinalSize 复制后的条目总数
     */
    void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);

    /**
     * @brief 客户端收到条目变更后：批量原子同步新 Spec 与叠层修正器、刷新 Tick 状态并广播差异
     * @param ChangedIndices 变更条目下标
     * @param FinalSize 复制后的条目总数
     */
    void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);

    /**
     * @brief 客户端移除前：批量回滚被删条目的属性修正器、当帧刷新 Tick 状态并广播差异
     * @param RemovedIndices 待移除条目下标
     * @param FinalSize 移除后的条目总数
     */
    void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize);
};

template<>
struct TStructOpsTypeTraits<FGameActiveBuffArray> : public TStructOpsTypeTraitsBase2<FGameActiveBuffArray> {
    enum { WithNetDeltaSerializer = true };
};
