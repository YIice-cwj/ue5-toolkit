// Copyright Epic Games, Inc. All Rights Reserved.

#include "Scheduling/PoolDeferredDispatcher.h"
#include "Managers/ActorPoolManager.h"
#include "Managers/UObjectPoolManager.h"
#include "GameFramework/Actor.h"

/**
 * @brief 异步延时将 Actor 加入归还队列（多线程/并发安全）
 * @param Actor 待归还的目标 Actor 实例
 * @return 成功入队返回 Success，队列已满或 Actor 无效返回 Failure
 */
TGameStatusResult<void> FPoolDeferredDispatcher::EnqueueDeferredRelease(AActor* Actor) {
    if (!IsValid(Actor)) {
        return TGameStatusResult<void>::Failure(1, TEXT("Actor 无效或为空"));
    }
    if (!DeferredReleaseBuffer.Enqueue(TWeakObjectPtr<AActor>(Actor))) {
        return TGameStatusResult<void>::Failure(2, TEXT("延迟回收缓冲区已满"));
    }
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 异步延时将纯 UObject 加入归还队列（多线程/并发安全）
 * @param Object 待归还的目标 UObject 实例
 * @return 成功入队返回 Success，队列已满或 Object 无效返回 Failure
 */
TGameStatusResult<void> FPoolDeferredDispatcher::EnqueueDeferredRelease(UObject* Object) {
    if (!IsValid(Object)) {
        return TGameStatusResult<void>::Failure(1, TEXT("UObject 无效或为空"));
    }
    if (!DeferredObjectReleaseBuffer.Enqueue(TWeakObjectPtr<UObject>(Object))) {
        return TGameStatusResult<void>::Failure(2, TEXT("延迟回收缓冲区已满"));
    }
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 分步批量处理延时归还队列中的 Actor 与 UObject
 * @param ActorManager Actor 池管理器引用
 * @param ObjectManager UObject 池管理器引用
 * @param MaxProcessCount 本次最大处理配额
 * @return 实际成功归还入池的实例总数
 */
int32 FPoolDeferredDispatcher::ProcessDeferredReleases(
    FActorPoolManager& ActorManager,
    FUObjectPoolManager& ObjectManager,
    const int32 MaxProcessCount
) {
    int32 ProcessedCount = 0;
    bool bHasMoreActors = true;
    bool bHasMoreObjects = true;

    while (ProcessedCount < MaxProcessCount && (bHasMoreActors || bHasMoreObjects)) {
        if (bHasMoreActors && ProcessedCount < MaxProcessCount) {
            TWeakObjectPtr<AActor> WeakActor;
            if (DeferredReleaseBuffer.Dequeue(WeakActor)) {
                if (AActor* Actor = WeakActor.Get()) {
                    if (IsValid(Actor)) {
                        ActorManager.ReleaseActor(Actor);
                        ++ProcessedCount;
                    }
                }
            } else {
                bHasMoreActors = false;
            }
        }

        if (bHasMoreObjects && ProcessedCount < MaxProcessCount) {
            TWeakObjectPtr<UObject> WeakObject;
            if (DeferredObjectReleaseBuffer.Dequeue(WeakObject)) {
                if (UObject* Object = WeakObject.Get()) {
                    if (IsValid(Object)) {
                        ObjectManager.ReleaseObject(Object);
                        ++ProcessedCount;
                    }
                }
            } else {
                bHasMoreObjects = false;
            }
        }
    }
    return ProcessedCount;
}

/**
 * @brief 清空延时归还环形队列中的所有待处理项
 */
void FPoolDeferredDispatcher::Reset() {
    TWeakObjectPtr<AActor> IgnoredActor;
    while (DeferredReleaseBuffer.Dequeue(IgnoredActor)) {
    }
    TWeakObjectPtr<UObject> IgnoredObject;
    while (DeferredObjectReleaseBuffer.Dequeue(IgnoredObject)) {
    }
}
