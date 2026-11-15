// Copyright Epic Games, Inc. All Rights Reserved.

#include "Scheduling/PoolDeferredDispatcher.h"
#include "Managers/ActorPoolManager.h"
#include "GameFramework/Actor.h"

/**
 * @brief 异步延时将 Actor 加入归还队列（多线程/并发安全）
 * @param Actor 待归还的目标 Actor 实例
 * @return 成功入队返回 true，队列已满或 Actor 无效返回 false
 */
bool FPoolDeferredDispatcher::EnqueueDeferredRelease(AActor* Actor) {
    if (!IsValid(Actor)) {
        return false;
    }
    return DeferredReleaseBuffer.Enqueue(TWeakObjectPtr<AActor>(Actor));
}

/**
 * @brief 分步批量处理延时归还队列中的 Actor
 * @param ActorManager Actor 池管理器引用
 * @param MaxProcessCount 本次最大处理配额
 * @return 实际成功归还入池的 Actor 数量
 */
int32 FPoolDeferredDispatcher::ProcessDeferredReleases(FActorPoolManager& ActorManager, const int32 MaxProcessCount) {
    int32 ProcessedCount = 0;
    TWeakObjectPtr<AActor> WeakActor;
    while (ProcessedCount < MaxProcessCount && DeferredReleaseBuffer.Dequeue(WeakActor)) {
        if (AActor* Actor = WeakActor.Get()) {
            if (IsValid(Actor)) {
                ActorManager.ReleaseActor(Actor);
                ++ProcessedCount;
            }
        }
    }
    return ProcessedCount;
}

/**
 * @brief 清空延时归还环形队列中的所有待处理项
 */
void FPoolDeferredDispatcher::Reset() {
    TWeakObjectPtr<AActor> Ignored;
    while (DeferredReleaseBuffer.Dequeue(Ignored)) {
    }
}
