// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectPool/ObjectPoolSubsystem.h"
#include "Async/FGameCoroAwaiters.h"
#include "Config/GameCompileSwitches.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"

namespace {

DEFINE_LOG_CATEGORY_STATIC(LogGameObjectPool, Log, All);

}  // namespace

UObjectPoolSubsystem::UObjectPoolSubsystem() = default;

UObjectPoolSubsystem::~UObjectPoolSubsystem() = default;

/**
 * @brief 获取对象池子系统单例引用
 * @param WorldContextObject 关联的世界上下文对象
 * @return 对象池子系统引用
 */
UObjectPoolSubsystem& UObjectPoolSubsystem::Get(const UObject* WorldContextObject) {
    UObjectPoolSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

/**
 * @brief 安全获取对象池子系统指针
 * @param WorldContextObject 关联的世界上下文对象
 * @return 子系统指针，未就绪返回 nullptr
 */
UObjectPoolSubsystem* UObjectPoolSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (!WorldContextObject) {
        return nullptr;
    }
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
        return World->GetSubsystem<UObjectPoolSubsystem>();
    }
    return nullptr;
}

/**
 * @brief 世界子系统初始化生命周期
 * @param Collection 子系统依赖集合
 */
void UObjectPoolSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    ActorManager.Initialize(GetWorld(), &GenerationTracker, this);
    ObjectManager.Initialize(this, &GenerationTracker);
}

/**
 * @brief 世界销毁生命周期回调
 */
void UObjectPoolSubsystem::Deinitialize() {
    ProcessDeferredReleases(1024);
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameObjectPool, Log, TEXT("【对象池】子系统注销，清空 %d 个 Actor 池与 %d 个 UObject 池。"),
            ActorManager.GetPoolCount(), ObjectManager.GetPoolCount());
    }
    ClearAllPools();
    GenerationTracker.Reset();
    Super::Deinitialize();
}

/**
 * @brief 同步预热指定类别的 Actor 对象池
 * @param ActorClass 目标 Actor 类
 * @param PrewarmCount 预热生成的空闲实例数量
 */
void UObjectPoolSubsystem::Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount) {
    ActorManager.Prewarm(ActorClass, PrewarmCount);
}

/**
 * @brief C++20 协程分帧渐进预热 Actor 对象池
 * @param ActorClass 目标 Actor 类
 * @param PrewarmCount 预热生成的空闲实例总数量
 * @param MaxSpawnsPerStep 单步最大生成实例数
 * @param CancellationToken 协作式取消令牌
 * @return 实际成功预热生成的实例总数异步协程任务
 */
TGameCoroTask<int32> UObjectPoolSubsystem::PrewarmAsync(
    TSubclassOf<AActor> ActorClass,
    int32 PrewarmCount,
    int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    co_await Game::Coro::BindContext(this, CancellationToken);
    if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract) || PrewarmCount <= 0 || CancellationToken.IsCancellationRequested()) {
        co_return 0;
    }

    const int32 BatchSize = FMath::Max(1, MaxSpawnsPerStep);
    int32 SpawnedCount = 0;

    for (int32 Index = 0; Index < PrewarmCount; ++Index) {
        if (CancellationToken.IsCancellationRequested()) {
            break;
        }

        const FPoolBucket* Bucket = ActorManager.FindBucket(ActorClass);
        if (Bucket != nullptr && Bucket->MaxIdleCount > 0 && Bucket->IdleActors.Num() >= Bucket->MaxIdleCount) {
            break;
        }

        if (!SpawnIdleActorToPool(ActorClass)) {
            break;
        }

        ++SpawnedCount;
        if ((SpawnedCount % BatchSize) == 0 && SpawnedCount < PrewarmCount) {
            co_await Game::Coro::YieldTimeSlice(this);
        }
    }

    co_return SpawnedCount;
}

/**
 * @brief 基于软对象路径的 C++20 协程流式异步预热
 * @param ActorClassPath 目标 Actor 类的软对象路径
 * @param PrewarmCount 预热生成的空闲实例总数量
 * @param MaxSpawnsPerStep 单步最大生成实例数
 * @param CancellationToken 协作式取消令牌
 * @return 实际成功预热生成的实例总数异步协程任务
 */
TGameCoroTask<int32> UObjectPoolSubsystem::PrewarmAsync(
    const FSoftObjectPath& ActorClassPath,
    int32 PrewarmCount,
    int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    co_await Game::Coro::BindContext(this, CancellationToken);
    if (!ActorClassPath.IsValid() || PrewarmCount <= 0 || CancellationToken.IsCancellationRequested()) {
        co_return 0;
    }

    UClass* LoadedClass = Cast<UClass>(ActorClassPath.ResolveObject());
    if (LoadedClass == nullptr) {
        TSharedPtr<FStreamableHandle> LoadHandle;
        if (UAssetManager::IsInitialized()) {
            LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(ActorClassPath);
        } else {
            static FStreamableManager StaticStreamable;
            LoadHandle = StaticStreamable.RequestAsyncLoad(ActorClassPath);
        }

        if (LoadHandle.IsValid()) {
            while (!LoadHandle->HasLoadCompleted() && !CancellationToken.IsCancellationRequested()) {
                co_await Game::Coro::YieldTimeSlice(this);
            }
        }
        LoadedClass = Cast<UClass>(ActorClassPath.ResolveObject());
    }

    if (LoadedClass == nullptr || !LoadedClass->IsChildOf(AActor::StaticClass()) || LoadedClass->HasAnyClassFlags(CLASS_Abstract)) {
        co_return 0;
    }

    if (CancellationToken.IsCancellationRequested()) {
        co_return 0;
    }

    const TGameStatusResult<int32> NestedResult = co_await PrewarmAsync(
        TSubclassOf<AActor>(LoadedClass),
        PrewarmCount,
        MaxSpawnsPerStep,
        CancellationToken
    );
    co_return NestedResult.IsSuccess() ? NestedResult.GetValue() : 0;
}

/**
 * @brief 设置指定 Actor 类别的最大空闲实例缓存容量
 * @param ActorClass 目标 Actor 类
 * @param InMaxIdleCount 最大空闲数
 */
void UObjectPoolSubsystem::SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, int32 InMaxIdleCount) {
    ActorManager.SetMaxPoolCapacity(ActorClass, InMaxIdleCount);
}

/**
 * @brief 配置指定 Actor 类别归还入池时的组件自动重置策略掩码
 * @param ActorClass 目标 Actor 类
 * @param InResetFlags 重置位掩码
 */
void UObjectPoolSubsystem::SetPoolResetFlags(TSubclassOf<AActor> ActorClass, EPoolResetFlags InResetFlags) {
    ActorManager.SetPoolResetFlags(ActorClass, InResetFlags);
}

/**
 * @brief 查询指定 Actor 类别当前的组件自动重置策略掩码
 * @param ActorClass 目标 Actor 类
 * @return 当前配置的组件重置策略掩码
 */
EPoolResetFlags UObjectPoolSubsystem::GetPoolResetFlags(TSubclassOf<AActor> ActorClass) const {
    return ActorManager.GetPoolResetFlags(ActorClass);
}

/**
 * @brief 从池中借出或生成 Actor 实例（蓝图轻量门面）
 * @param ActorClass 目标类
 * @param SpawnTransform 初始空间变换
 * @return 激活的 Actor 实例指针
 */
AActor* UObjectPoolSubsystem::K2_AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform) {
    const TGameStatusResult<AActor*> Result = AcquireActor<AActor>(ActorClass, SpawnTransform);
    return Result.IsSuccess() ? Result.GetValue() : nullptr;
}

/**
 * @brief 内部借出或生成 Actor 实例核心实现
 * @param ActorClass 目标类
 * @param SpawnTransform 初始空间变换
 * @return 成功返回 Actor 指针，失败返回 Failure
 */
TGameStatusResult<AActor*> UObjectPoolSubsystem::AcquireActorInternal(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform) {
    return ActorManager.AcquireActor(ActorClass, SpawnTransform);
}

/**
 * @brief 将已借出的 Actor 归还回池休眠（蓝图轻量门面）
 * @param Actor 待归还的实例
 */
void UObjectPoolSubsystem::K2_ReleaseActor(AActor* Actor) {
    ReleaseActor(Actor);
}

/**
 * @brief 将已借出的 Actor 归还回池休眠（强类型契约）
 * @param Actor 待归还的实例
 * @return 成功归还返回 Success，未借出或已销毁返回 Failure
 */
TGameStatusResult<void> UObjectPoolSubsystem::ReleaseActor(AActor* Actor) {
    return ActorManager.ReleaseActor(Actor);
}

/**
 * @brief 在工作线程或异步任务中以无锁方式安全将 Actor 压入延迟归还队列
 * @param Actor 待归还的实例
 * @return 成功压入返回 true，队列已满返回 false
 */
bool UObjectPoolSubsystem::ReleaseActorDeferred(AActor* Actor) {
    if (!Actor) {
        return false;
    }
    return DeferredReleaseBuffer.Enqueue(Actor);
}

/**
 * @brief 主线程批量收割并完成延迟队列中的 Actor 归还
 * @param MaxProcessCount 单次最大处理实例数
 * @return 实际成功收割归还的实例数
 */
int32 UObjectPoolSubsystem::ProcessDeferredReleases(int32 MaxProcessCount) {
    if (!IsInGameThread()) {
        return 0;
    }

    int32 ProcessedCount = 0;
    const int32 Limit = FMath::Max(1, MaxProcessCount);

    while (ProcessedCount < Limit) {
        TOptional<TWeakObjectPtr<AActor>> Item = DeferredReleaseBuffer.Dequeue();
        if (!Item.IsSet()) {
            break;
        }
        if (AActor* Actor = Item.GetValue().Get()) {
            ReleaseActor(Actor);
            ++ProcessedCount;
        }
    }

    return ProcessedCount;
}

/**
 * @brief 查询指定 Actor 类别的池容量与活跃统计快照
 * @param ActorClass 目标 Actor 类
 * @return 池状态统计结构体快照
 */
FPoolStats UObjectPoolSubsystem::GetPoolStats(TSubclassOf<AActor> ActorClass) const {
    return ActorManager.GetPoolStats(ActorClass);
}

/**
 * @brief 清空并销毁指定 Actor 类别的所有空闲实例
 * @param ActorClass 目标类
 */
void UObjectPoolSubsystem::ClearPool(TSubclassOf<AActor> ActorClass) {
    ActorManager.ClearPool(ActorClass);
}

/**
 * @brief 清空并销毁所有类别的空闲实例
 */
void UObjectPoolSubsystem::ClearAllPools() {
    ActorManager.ClearAllPools();
    ClearAllObjectPools();
}

/**
 * @brief 同步预热指定非 Actor UObject 类别的对象池
 * @param ObjectClass 目标 UObject 派生类
 * @param PrewarmCount 预热生成的空闲实例数量
 */
void UObjectPoolSubsystem::PrewarmObjectPool(TSubclassOf<UObject> ObjectClass, int32 PrewarmCount) {
    ObjectManager.Prewarm(ObjectClass, PrewarmCount);
}

/**
 * @brief 设置指定非 Actor UObject 类别的最大空闲缓存容量
 * @param ObjectClass 目标 UObject 派生类
 * @param InMaxIdleCount 最大空闲数
 */
void UObjectPoolSubsystem::SetMaxObjectPoolCapacity(TSubclassOf<UObject> ObjectClass, int32 InMaxIdleCount) {
    ObjectManager.SetMaxCapacity(ObjectClass, InMaxIdleCount);
}

/**
 * @brief 从池中借出或构建非 Actor UObject 实例（蓝图轻量门面）
 * @param ObjectClass 目标 UObject 派生类
 * @return 激活的 UObject 实例指针
 */
UObject* UObjectPoolSubsystem::K2_AcquireObject(TSubclassOf<UObject> ObjectClass) {
    const TGameStatusResult<UObject*> Result = AcquireObject<UObject>(ObjectClass);
    return Result.IsSuccess() ? Result.GetValue() : nullptr;
}

/**
 * @brief 内部借出或构建非 Actor UObject 实例核心实现
 * @param ObjectClass 目标 UObject 派生类
 * @return 成功返回 UObject 指针，失败返回 Failure
 */
TGameStatusResult<UObject*> UObjectPoolSubsystem::AcquireObjectInternal(TSubclassOf<UObject> ObjectClass) {
    return ObjectManager.AcquireObject(ObjectClass);
}

/**
 * @brief 将已借出的非 Actor UObject 归还回池缓存（蓝图轻量门面）
 * @param Object 待归还的 UObject 实例
 */
void UObjectPoolSubsystem::K2_ReleaseObject(UObject* Object) {
    ReleaseObject(Object);
}

/**
 * @brief 将已借出的非 Actor UObject 归还回池缓存（强类型契约）
 * @param Object 待归还的 UObject 实例
 * @return 成功归还返回 Success，未借出或已销毁返回 Failure
 */
TGameStatusResult<void> UObjectPoolSubsystem::ReleaseObject(UObject* Object) {
    return ObjectManager.ReleaseObject(Object);
}

/**
 * @brief 查询指定非 Actor UObject 类别的池容量与活跃统计快照
 * @param ObjectClass 目标 UObject 派生类
 * @return 池状态统计结构体
 */
FPoolStats UObjectPoolSubsystem::GetObjectPoolStats(TSubclassOf<UObject> ObjectClass) const {
    return ObjectManager.GetPoolStats(ObjectClass);
}

/**
 * @brief 清空并回收指定非 Actor UObject 类别的所有空闲实例
 * @param ObjectClass 目标 UObject 派生类
 */
void UObjectPoolSubsystem::ClearObjectPool(TSubclassOf<UObject> ObjectClass) {
    ObjectManager.ClearPool(ObjectClass);
}

/**
 * @brief 清空并回收所有非 Actor UObject 类别的空闲实例
 */
void UObjectPoolSubsystem::ClearAllObjectPools() {
    ObjectManager.ClearAllPools();
}

/**
 * @brief 校验 Actor 实例句柄记录的世代是否与池内活跃代数一致
 * @param Actor 目标 Actor 实例
 * @param InGeneration 待校验世代
 * @return 活跃且世代一致返回 true
 */
bool UObjectPoolSubsystem::ValidateHandleGeneration(const AActor* Actor, uint32 InGeneration) const {
    if (!IsValid(Actor) || InGeneration == 0) {
        return false;
    }
    if (!GenerationTracker.ValidateGeneration(Actor, InGeneration)) {
        return false;
    }
    const FPoolBucket* Bucket = ActorManager.FindBucket(Actor->GetClass());
    return Bucket != nullptr && Bucket->ActiveActorSet.Contains(const_cast<AActor*>(Actor));
}

/**
 * @brief 校验 UObject 实例句柄记录的世代是否与池内活跃代数一致
 * @param Object 目标 UObject 实例
 * @param InGeneration 待校验世代
 * @return 活跃且世代一致返回 true
 */
bool UObjectPoolSubsystem::ValidateHandleGeneration(const UObject* Object, uint32 InGeneration) const {
    if (!IsValid(Object) || InGeneration == 0) {
        return false;
    }
    if (!GenerationTracker.ValidateGeneration(Object, InGeneration)) {
        return false;
    }
    const FObjectPoolBucket* Bucket = ObjectManager.FindBucket(Object->GetClass());
    return Bucket != nullptr && Bucket->ActiveObjectSet.Contains(const_cast<UObject*>(Object));
}

/**
 * @brief 获取 Actor 实例当前的有效借出世代代数
 * @param Actor 目标 Actor 实例
 * @return 世代代数
 */
uint32 UObjectPoolSubsystem::GetActorGeneration(const AActor* Actor) const {
    return GenerationTracker.FindGeneration(Actor);
}

/**
 * @brief 获取 UObject 实例当前的有效借出世代代数
 * @param Object 目标 UObject 实例
 * @return 世代代数
 */
uint32 UObjectPoolSubsystem::GetObjectGeneration(const UObject* Object) const {
    return GenerationTracker.FindGeneration(Object);
}

/**
 * @brief 生成单个空闲 Actor 实例并放入空闲池
 * @param ActorClass 目标 Actor 类
 * @return 成功生成并入池返回 true
 */
bool UObjectPoolSubsystem::SpawnIdleActorToPool(TSubclassOf<AActor> ActorClass) {
    return ActorManager.SpawnIdleActorToPool(ActorClass);
}

/**
 * @brief 响应池化 Actor 外部意外销毁的回调
 * @param DestroyedActor 被销毁的 Actor 实例
 */
void UObjectPoolSubsystem::OnPooledActorDestroyed(AActor* DestroyedActor) {
    ActorManager.OnPooledActorDestroyed(DestroyedActor);
}
