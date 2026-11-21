/**
 * @file FPoolDeferredDispatcher.h
 * @brief 跨线程无锁延迟归还内部调度控制器（单队列堆分配、CDO 零占用、Vyukov MPMC 安全入队）
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/TLockFreeRingBuffer.h"
#include "Templates/UniquePtr.h"
#include "Types/TGameResult.h"
#include "UObject/WeakObjectPtr.h"

class AActor;
class UObject;
class FActorPoolManager;
class FUObjectPoolManager;

/**
 * @brief 跨线程无锁延迟归还调度控制器（内部持有单一堆分配 MPMC 环形缓冲，按需初始化释放）
 */
class FPoolDeferredDispatcher {
private:
    struct FDeferredReleaseEntry {
        FWeakObjectPtr ObjectWeak;
        bool bIsActor = false;
    };

    static constexpr uint32 BufferCapacity = 1024u;
    using FRingBufferType = TLockFreeRingBuffer<FDeferredReleaseEntry, BufferCapacity>;

public:
    FPoolDeferredDispatcher() = default;
    ~FPoolDeferredDispatcher() = default;

    FPoolDeferredDispatcher(const FPoolDeferredDispatcher&) = delete;
    FPoolDeferredDispatcher& operator=(const FPoolDeferredDispatcher&) = delete;

    /**
     * @brief 在子系统初始化期于堆上分配无锁环形缓冲（避免在 CDO 上静态占用 64 KB 内存）
     */
    void Initialize();

    /**
     * @brief 清空队列并释放底层无锁环形缓冲内存
     */
    void Deinitialize();

    /**
     * @brief 异步延时将 Actor 加入无锁归还队列（多线程/并发安全）
     * @param Actor 待归还的目标 Actor 实例
     * @return 成功入队返回 Success，队列满/未初始化或 Actor 无效返回 Failure
     */
    [[nodiscard]] TGameStatusResult<void> EnqueueDeferredRelease(AActor* Actor);

    /**
     * @brief 异步延时将纯 UObject 加入无锁归还队列（多线程/并发安全，拒绝 AActor 派生实例）
     * @param Object 待归还的目标 UObject 实例
     * @return 成功入队返回 Success，队列满/未初始化、对象无效或为 AActor 返回 Failure
     */
    [[nodiscard]] TGameStatusResult<void> EnqueueDeferredRelease(UObject* Object);

    /**
     * @brief 在 GameThread 分步批量收割延迟归还队列中的 Actor 与 UObject（仅统计实际成功归还数）
     * @param ActorManager Actor 池管理器引用
     * @param ObjectManager UObject 池管理器引用
     * @param MaxProcessCount 本次最大成功归还配额（<=0 直接返回 0）
     * @return 实际成功归还入池的实例总数
     */
    int32 ProcessDeferredReleases(
        FActorPoolManager& ActorManager,
        FUObjectPoolManager& ObjectManager,
        int32 MaxProcessCount = 64
    );

    /**
     * @brief 查询延迟归还队列当前近似待处理条目数
     * @return 近似积压条目数
     */
    [[nodiscard]] int32 GetPendingCount() const noexcept;

private:
    TUniquePtr<FRingBufferType> ReleaseBuffer;
};
