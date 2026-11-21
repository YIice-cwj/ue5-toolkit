/**
 * @file FPoolDeferredDispatcher.cpp
 * @brief 跨线程无锁延迟归还内部调度控制器实现
 */

#include "Scheduling/FPoolDeferredDispatcher.h"

#include "Core/PoolErrorCodes.h"
#include "GameFramework/Actor.h"
#include "Managers/FActorPoolManager.h"
#include "Managers/FUObjectPoolManager.h"

/**
 * @brief 在子系统初始化期于堆上分配无锁环形缓冲
 */
void FPoolDeferredDispatcher::Initialize() {
    if (!ReleaseBuffer.IsValid()) {
        ReleaseBuffer = MakeUnique<FRingBufferType>();
    }
}

/**
 * @brief 清空队列并释放底层无锁环形缓冲内存
 */
void FPoolDeferredDispatcher::Deinitialize() {
    if (ReleaseBuffer.IsValid()) {
        ReleaseBuffer->Clear();
        ReleaseBuffer.Reset();
    }
}

/**
 * @brief 异步延时将 Actor 加入无锁归还队列（多线程/并发安全）
 * @param Actor 待归还的目标 Actor 实例
 * @return 成功入队返回 Success，队列满/未初始化或 Actor 无效返回 Failure
 */
TGameStatusResult<void> FPoolDeferredDispatcher::EnqueueDeferredRelease(AActor* Actor) {
    if (Actor == nullptr || !IsValid(Actor)) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidInstance, TEXT("Actor is null or pending kill"))
        );
    }
    if (!ReleaseBuffer.IsValid() || !ReleaseBuffer->Emplace(FDeferredReleaseEntry{FWeakObjectPtr(Actor), true})) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::DeferredQueueFull, TEXT("Deferred release ring buffer is full or uninitialized"))
        );
    }
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 异步延时将纯 UObject 加入无锁归还队列（多线程/并发安全，拒绝 AActor 派生实例）
 * @param Object 待归还的目标 UObject 实例
 * @return 成功入队返回 Success，队列满/未初始化、对象无效或为 AActor 返回 Failure
 */
TGameStatusResult<void> FPoolDeferredDispatcher::EnqueueDeferredRelease(UObject* Object) {
    if (Object == nullptr || !IsValid(Object)) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidInstance, TEXT("UObject is null or marked as garbage"))
        );
    }
    if (Object->IsA<AActor>()) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidClass, TEXT("AActor instances must use ReleaseActorDeferred"))
        );
    }
    if (!ReleaseBuffer.IsValid() || !ReleaseBuffer->Emplace(FDeferredReleaseEntry{FWeakObjectPtr(Object), false})) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::DeferredQueueFull, TEXT("Deferred release ring buffer is full or uninitialized"))
        );
    }
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 在 GameThread 分步批量收割延迟归还队列中的 Actor 与 UObject（仅统计实际成功归还数）
 * @param ActorManager Actor 池管理器引用
 * @param ObjectManager UObject 池管理器引用
 * @param MaxProcessCount 本次最大成功归还配额（<=0 直接返回 0）
 * @return 实际成功归还入池的实例总数
 */
int32 FPoolDeferredDispatcher::ProcessDeferredReleases(
    FActorPoolManager& ActorManager,
    FUObjectPoolManager& ObjectManager,
    const int32 MaxProcessCount
) {
    TRACE_CPUPROFILER_EVENT_SCOPE(GamePool_ProcessDeferredReleases);

    if (MaxProcessCount <= 0 || !ReleaseBuffer.IsValid()) {
        return 0;
    }

    int32 ProcessedCount = 0;
    FDeferredReleaseEntry Entry;
    while (ProcessedCount < MaxProcessCount && ReleaseBuffer->Dequeue(Entry)) {
        UObject* RawObject = Entry.ObjectWeak.Get();
        if (!IsValid(RawObject)) {
            continue;
        }
        const TGameStatusResult<void> ReleaseResult = Entry.bIsActor
            ? ActorManager.ReleaseActor(Cast<AActor>(RawObject))
            : ObjectManager.ReleaseObject(RawObject);
        if (ReleaseResult.IsSuccess()) {
            ++ProcessedCount;
        }
    }
    return ProcessedCount;
}

/**
 * @brief 查询延迟归还队列当前近似待处理条目数
 * @return 近似积压条目数
 */
int32 FPoolDeferredDispatcher::GetPendingCount() const noexcept {
    return ReleaseBuffer.IsValid() ? static_cast<int32>(ReleaseBuffer->Num()) : 0;
}
