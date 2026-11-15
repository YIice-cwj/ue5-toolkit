// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/TLockFreeRingBuffer.h"
#include "Types/TGameResult.h"
#include "UObject/WeakObjectPtr.h"

class AActor;
class FActorPoolManager;

/**
 * @brief 延时入池调度控制器，封装硬件级无锁环形队列并提供批量延迟归还调度
 */
class GAMEPOOL_API FPoolDeferredDispatcher {
public:
    FPoolDeferredDispatcher() = default;
    ~FPoolDeferredDispatcher() = default;

    FPoolDeferredDispatcher(const FPoolDeferredDispatcher&) = delete;
    FPoolDeferredDispatcher& operator=(const FPoolDeferredDispatcher&) = delete;

    /**
     * @brief 异步延时将 Actor 加入归还队列（多线程/并发安全）
     * @param Actor 待归还的目标 Actor 实例
     * @return 成功入队返回 Success，队列已满或 Actor 无效返回 Failure
     */
    [[nodiscard]] TGameStatusResult<void> EnqueueDeferredRelease(AActor* Actor);

    /**
     * @brief 分步批量处理延时归还队列中的 Actor
     * @param ActorManager Actor 池管理器引用
     * @param MaxProcessCount 本次最大处理配额
     * @return 实际成功归还入池的 Actor 数量
     */
    int32 ProcessDeferredReleases(FActorPoolManager& ActorManager, int32 MaxProcessCount = 64);

    /**
     * @brief 清空延时归还环形队列中的所有待处理项
     */
    void Reset();

private:
    TLockFreeRingBuffer<TWeakObjectPtr<AActor>, 1024> DeferredReleaseBuffer;
};
