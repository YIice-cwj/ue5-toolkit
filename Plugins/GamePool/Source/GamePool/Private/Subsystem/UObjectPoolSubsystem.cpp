/**
 * @file UObjectPoolSubsystem.cpp
 * @brief 世界级对象池外观门面子系统实现（Pimpl 内部组合与统一 GC 追踪）
 */

#include "Subsystem/UObjectPoolSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Handle/FPoolGenerationTracker.h"
#include "Managers/FActorPoolManager.h"
#include "Managers/FUObjectPoolManager.h"
#include "Resilience/FPoolResilienceController.h"
#include "Scheduling/FPoolAsyncLoader.h"
#include "Scheduling/FPoolDeferredDispatcher.h"
#include "Scheduling/FPoolEvictionScheduler.h"

struct UObjectPoolSubsystem::FImpl {
    FPoolGenerationTracker GenerationTracker;
    FActorPoolManager ActorManager;
    FUObjectPoolManager ObjectManager;
    FPoolDeferredDispatcher DeferredDispatcher;
    FPoolEvictionScheduler EvictionScheduler;
    FPoolResilienceController ResilienceController;
};

/**
 * @brief 默认构造函数
 */
UObjectPoolSubsystem::UObjectPoolSubsystem() = default;

/**
 * @brief 虚析构函数
 */
UObjectPoolSubsystem::~UObjectPoolSubsystem() = default;

/**
 * @brief 初始化对象池子系统，创建底层管理器、调度器与弹性控制器上下文
 * @param Collection 子系统集合引用
 */
void UObjectPoolSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    Impl = MakeShared<FImpl>();

    UWorld* World = GetWorld();
    Impl->ActorManager.Initialize(World, &Impl->GenerationTracker, this);
    Impl->ObjectManager.Initialize(this, &Impl->GenerationTracker);
    Impl->DeferredDispatcher.Initialize();
    Impl->EvictionScheduler.Initialize(this, &Impl->ActorManager, &Impl->ObjectManager);
    Impl->ResilienceController.Initialize(this, &Impl->ActorManager, &Impl->ObjectManager);
}

/**
 * @brief 反初始化对象池子系统，清理全部池实例、Stat 计数、延迟队列、调度器与容灾委托
 */
void UObjectPoolSubsystem::Deinitialize() {
    if (Impl.IsValid()) {
        Impl->ResilienceController.Deinitialize();
        Impl->EvictionScheduler.Deinitialize();
        Impl->DeferredDispatcher.Deinitialize();
        Impl->ActorManager.Deinitialize();
        Impl->ObjectManager.Deinitialize();
        Impl->GenerationTracker.Reset();
        Impl.Reset();
    }
    Super::Deinitialize();
}

/**
 * @brief 同步预热指定数量的 Actor 到空闲对象池
 * @param ActorClass 目标 Actor 类
 * @param PrewarmCount 预热生成的实例数量
 */
void UObjectPoolSubsystem::Prewarm(TSubclassOf<AActor> ActorClass, const int32 PrewarmCount) {
    if (Impl.IsValid()) {
        Impl->ActorManager.Prewarm(ActorClass, PrewarmCount);
    }
}

/**
 * @brief 基于 C++20 协程的分步异步预热 Actor
 * @param ActorClass 目标 Actor 类
 * @param PrewarmCount 预热生成的空闲实例总数量
 * @param MaxSpawnsPerStep 单步最大生成实例数
 * @param CancellationToken 协作式取消令牌
 * @return 实际成功预热生成的实例总数异步协程任务
 */
TGameCoroTask<int32> UObjectPoolSubsystem::PrewarmAsync(
    const TSubclassOf<AActor> ActorClass,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    checkf(Impl.IsValid(), TEXT("UObjectPoolSubsystem::PrewarmAsync called before Initialize"));
    return FPoolAsyncLoader::PrewarmActorsAsync(
        this,
        TWeakPtr<const void>(Impl),
        Impl->ActorManager,
        ActorClass,
        PrewarmCount,
        MaxSpawnsPerStep,
        MoveTemp(CancellationToken)
    );
}

/**
 * @brief 基于软对象路径的 C++20 协程事件驱动流式加载与分步异步预热 Actor
 * @param ActorClassPath 目标 Actor 类的软对象路径
 * @param PrewarmCount 预热生成的空闲实例总数量
 * @param MaxSpawnsPerStep 单步最大生成实例数
 * @param CancellationToken 协作式取消令牌
 * @return 实际成功预热生成的实例总数异步协程任务
 */
TGameCoroTask<int32> UObjectPoolSubsystem::PrewarmAsync(
    const FSoftObjectPath& ActorClassPath,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    checkf(Impl.IsValid(), TEXT("UObjectPoolSubsystem::PrewarmAsync called before Initialize"));
    return FPoolAsyncLoader::PrewarmActorsByPathAsync(
        this,
        TWeakPtr<const void>(Impl),
        Impl->ActorManager,
        ActorClassPath,
        PrewarmCount,
        MaxSpawnsPerStep,
        MoveTemp(CancellationToken)
    );
}

/**
 * @brief 基于强类型软类引用的 C++20 协程流式加载与分步异步预热 Actor
 * @param ActorSoftClass 目标 Actor 软类引用
 * @param PrewarmCount 预热生成的空闲实例总数量
 * @param MaxSpawnsPerStep 单步最大生成实例数
 * @param CancellationToken 协作式取消令牌
 * @return 实际成功预热生成的实例总数异步协程任务
 */
TGameCoroTask<int32> UObjectPoolSubsystem::PrewarmAsync(
    const TSoftClassPtr<AActor>& ActorSoftClass,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    return PrewarmAsync(ActorSoftClass.ToSoftObjectPath(), PrewarmCount, MaxSpawnsPerStep, MoveTemp(CancellationToken));
}

/**
 * @brief 设置指定 Actor 类别的最大空闲实例缓存容量
 * @param ActorClass 目标 Actor 类
 * @param InMaxIdleCount 最大空闲数
 */
void UObjectPoolSubsystem::SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, const int32 InMaxIdleCount) {
    if (Impl.IsValid()) {
        Impl->ActorManager.SetMaxPoolCapacity(ActorClass, InMaxIdleCount);
    }
}

/**
 * @brief 配置指定 Actor 类别归还入池时的组件自动重置策略掩码
 * @param ActorClass 目标 Actor 类
 * @param InResetFlags 重置位掩码
 */
void UObjectPoolSubsystem::SetPoolResetFlags(TSubclassOf<AActor> ActorClass, const EPoolResetFlags InResetFlags) {
    if (Impl.IsValid()) {
        Impl->ActorManager.SetPoolResetFlags(ActorClass, InResetFlags);
    }
}

/**
 * @brief 查询指定 Actor 类别当前的组件自动重置策略掩码
 * @param ActorClass 目标 Actor 类
 * @return 当前配置的组件重置策略掩码
 */
EPoolResetFlags UObjectPoolSubsystem::GetPoolResetFlags(TSubclassOf<AActor> ActorClass) const {
    return Impl.IsValid() ? Impl->ActorManager.GetPoolResetFlags(ActorClass) : EPoolResetFlags::All;
}

/**
 * @brief 从池中借出或生成 Actor 实例（蓝图专用外观方法）
 * @param ActorClass 目标 Actor 类
 * @param SpawnTransform 初始空间变换
 * @return 借出的 Actor 原生指针，失败返回 nullptr
 */
AActor* UObjectPoolSubsystem::K2_AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform) {
    const TGameStatusResult<AActor*> Result = AcquireActor<AActor>(ActorClass, SpawnTransform);
    return Result.IsSuccess() ? Result.GetValue() : nullptr;
}

/**
 * @brief 将已借出的 Actor 归还回池缓存（蓝图专用外观方法）
 * @param Actor 待归还的 Actor 实例
 */
void UObjectPoolSubsystem::K2_ReleaseActor(AActor* Actor) {
    (void)ReleaseActor(Actor);
}

/**
 * @brief 将已借出的 Actor 归还回池缓存
 * @param Actor 待归还的 Actor 实例
 * @return 归还状态结果
 */
TGameStatusResult<void> UObjectPoolSubsystem::ReleaseActor(AActor* Actor) {
    if (!Impl.IsValid()) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidContext, TEXT("UObjectPoolSubsystem is not initialized"))
        );
    }
    return Impl->ActorManager.ReleaseActor(Actor);
}

/**
 * @brief 异步延时将 Actor 加入无锁归还队列（并发/多线程安全）
 * @param Actor 待归还的目标 Actor 实例
 * @return 成功入队返回 Success，失败返回 Failure
 */
TGameStatusResult<void> UObjectPoolSubsystem::ReleaseActorDeferred(AActor* Actor) {
    if (!Impl.IsValid()) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidContext, TEXT("UObjectPoolSubsystem is not initialized"))
        );
    }
    return Impl->DeferredDispatcher.EnqueueDeferredRelease(Actor);
}

/**
 * @brief 查询指定 Actor 类别的池容量与活跃统计快照
 * @param ActorClass 目标 Actor 类
 * @return 池状态统计结构体
 */
FPoolStats UObjectPoolSubsystem::GetPoolStats(TSubclassOf<AActor> ActorClass) const {
    return Impl.IsValid() ? Impl->ActorManager.GetPoolStats(ActorClass) : FPoolStats();
}

/**
 * @brief 清空指定 Actor 类别的所有空闲实例并释放资源
 * @param ActorClass 目标 Actor 类
 */
void UObjectPoolSubsystem::ClearPool(TSubclassOf<AActor> ActorClass) {
    if (Impl.IsValid()) {
        Impl->ActorManager.ClearPool(ActorClass);
    }
}

/**
 * @brief 清空所有 Actor 类别池中的全部空闲实例
 */
void UObjectPoolSubsystem::ClearAllActorPools() {
    if (Impl.IsValid()) {
        Impl->ActorManager.ClearAllPools();
    }
}

/**
 * @brief 同步预热指定数量的纯 UObject 到对象池
 * @param ObjectClass 目标 UObject 类
 * @param PrewarmCount 预热生成的实例数量
 */
void UObjectPoolSubsystem::PrewarmObjectPool(TSubclassOf<UObject> ObjectClass, const int32 PrewarmCount) {
    if (Impl.IsValid()) {
        Impl->ObjectManager.Prewarm(ObjectClass, PrewarmCount);
    }
}

/**
 * @brief 基于 C++20 协程的分步异步预热纯 UObject 对象池
 * @param ObjectClass 目标 UObject 类
 * @param PrewarmCount 预热生成的总空闲实例数
 * @param MaxSpawnsPerStep 单步最大生成实例数
 * @param CancellationToken 协作式取消令牌
 * @return 异步协程任务，返回实际成功实例化的数量
 */
TGameCoroTask<int32> UObjectPoolSubsystem::PrewarmObjectPoolAsync(
    const TSubclassOf<UObject> ObjectClass,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    checkf(Impl.IsValid(), TEXT("UObjectPoolSubsystem::PrewarmObjectPoolAsync called before Initialize"));
    return FPoolAsyncLoader::PrewarmObjectsAsync(
        this,
        TWeakPtr<const void>(Impl),
        Impl->ObjectManager,
        ObjectClass,
        PrewarmCount,
        MaxSpawnsPerStep,
        MoveTemp(CancellationToken)
    );
}

/**
 * @brief 基于软对象路径的 C++20 协程事件驱动流式加载与分步异步预热纯 UObject 对象池
 * @param ObjectClassPath 目标 UObject 类的软对象路径
 * @param PrewarmCount 预热生成的总空闲实例数
 * @param MaxSpawnsPerStep 单步最大生成实例数
 * @param CancellationToken 协作式取消令牌
 * @return 异步协程任务，返回实际成功实例化的数量
 */
TGameCoroTask<int32> UObjectPoolSubsystem::PrewarmObjectPoolAsync(
    const FSoftObjectPath& ObjectClassPath,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    checkf(Impl.IsValid(), TEXT("UObjectPoolSubsystem::PrewarmObjectPoolAsync called before Initialize"));
    return FPoolAsyncLoader::PrewarmObjectsByPathAsync(
        this,
        TWeakPtr<const void>(Impl),
        Impl->ObjectManager,
        ObjectClassPath,
        PrewarmCount,
        MaxSpawnsPerStep,
        MoveTemp(CancellationToken)
    );
}

/**
 * @brief 基于强类型软类引用的 C++20 协程流式加载与分步异步预热纯 UObject 对象池
 * @param ObjectSoftClass 目标 UObject 软类引用
 * @param PrewarmCount 预热生成的总空闲实例数
 * @param MaxSpawnsPerStep 单步最大生成实例数
 * @param CancellationToken 协作式取消令牌
 * @return 异步协程任务，返回实际成功实例化的数量
 */
TGameCoroTask<int32> UObjectPoolSubsystem::PrewarmObjectPoolAsync(
    const TSoftClassPtr<UObject>& ObjectSoftClass,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    return PrewarmObjectPoolAsync(
        ObjectSoftClass.ToSoftObjectPath(),
        PrewarmCount,
        MaxSpawnsPerStep,
        MoveTemp(CancellationToken)
    );
}

/**
 * @brief 设置指定非 Actor UObject 类别的最大空闲缓存容量
 * @param ObjectClass 目标 UObject 派生类
 * @param InMaxIdleCount 最大空闲数
 */
void UObjectPoolSubsystem::SetMaxObjectPoolCapacity(TSubclassOf<UObject> ObjectClass, const int32 InMaxIdleCount) {
    if (Impl.IsValid()) {
        Impl->ObjectManager.SetMaxPoolCapacity(ObjectClass, InMaxIdleCount);
    }
}

/**
 * @brief 从池中借出或构建非 Actor UObject 实例（蓝图专用外观方法）
 * @param ObjectClass 目标 UObject 派生类
 * @return 激活的 UObject 实例指针，失败返回 nullptr
 */
UObject* UObjectPoolSubsystem::K2_AcquireObject(TSubclassOf<UObject> ObjectClass) {
    const TGameStatusResult<UObject*> Result = AcquireObject<UObject>(ObjectClass);
    return Result.IsSuccess() ? Result.GetValue() : nullptr;
}

/**
 * @brief 将已借出的非 Actor UObject 归还回池缓存（蓝图专用外观方法）
 * @param Object 待归还的 UObject 实例
 */
void UObjectPoolSubsystem::K2_ReleaseObject(UObject* Object) {
    (void)ReleaseObject(Object);
}

/**
 * @brief 将已借出的非 Actor UObject 归还回池缓存
 * @param Object 待归还的 UObject 实例
 * @return 归还状态结果
 */
TGameStatusResult<void> UObjectPoolSubsystem::ReleaseObject(UObject* Object) {
    if (!Impl.IsValid()) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidContext, TEXT("UObjectPoolSubsystem is not initialized"))
        );
    }
    return Impl->ObjectManager.ReleaseObject(Object);
}

/**
 * @brief 异步延时将纯 UObject 加入无锁归还队列（并发/多线程安全）
 * @param Object 待归还的目标 UObject 实例
 * @return 成功入队返回 Success，失败返回 Failure
 */
TGameStatusResult<void> UObjectPoolSubsystem::ReleaseObjectDeferred(UObject* Object) {
    if (!Impl.IsValid()) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidContext, TEXT("UObjectPoolSubsystem is not initialized"))
        );
    }
    return Impl->DeferredDispatcher.EnqueueDeferredRelease(Object);
}

/**
 * @brief 查询指定纯 UObject 类的池容量与活跃统计快照
 * @param ObjectClass 目标类
 * @return 池状态统计结构体
 */
FPoolStats UObjectPoolSubsystem::GetObjectPoolStats(TSubclassOf<UObject> ObjectClass) const {
    return Impl.IsValid() ? Impl->ObjectManager.GetPoolStats(ObjectClass) : FPoolStats();
}

/**
 * @brief 清空指定纯 UObject 类别的所有空闲实例并释放垃圾回收引用
 * @param ObjectClass 目标类
 */
void UObjectPoolSubsystem::ClearObjectPool(TSubclassOf<UObject> ObjectClass) {
    if (Impl.IsValid()) {
        Impl->ObjectManager.ClearPool(ObjectClass);
    }
}

/**
 * @brief 清空所有非 Actor 纯 UObject 类别池中的全部空闲实例
 */
void UObjectPoolSubsystem::ClearAllObjectPools() {
    if (Impl.IsValid()) {
        Impl->ObjectManager.ClearAllPools();
    }
}

/**
 * @brief 清空所有池类别（Actor 与纯 UObject）中的全部空闲实例
 */
void UObjectPoolSubsystem::ClearAllPools() {
    ClearAllActorPools();
    ClearAllObjectPools();
    if (Impl.IsValid()) {
        Impl->GenerationTracker.CompactStaleEntries();
    }
}

/**
 * @brief 在 GameThread 批量收割无锁延迟归还队列中的待归还实例
 * @param MaxProcessCount 本次最大处理配额
 * @return 实际成功归还入池的实例总数
 */
int32 UObjectPoolSubsystem::ProcessDeferredReleases(const int32 MaxProcessCount) {
    if (!Impl.IsValid()) {
        return 0;
    }
    return Impl->DeferredDispatcher.ProcessDeferredReleases(Impl->ActorManager, Impl->ObjectManager, MaxProcessCount);
}

/**
 * @brief 查询当前无锁延迟归还队列中的近似待处理条目数量
 * @return 近似积压数量
 */
int32 UObjectPoolSubsystem::GetPendingDeferredReleaseCount() const noexcept {
    return Impl.IsValid() ? Impl->DeferredDispatcher.GetPendingCount() : 0;
}

/**
 * @brief 配置指定 Actor 类别的闲置弹性缩容与淘汰策略
 * @param ActorClass 目标 Actor 类
 * @param Settings 淘汰策略配置
 */
void UObjectPoolSubsystem::SetActorPoolEvictionSettings(TSubclassOf<AActor> ActorClass, const FPoolEvictionSettings& Settings) {
    if (!Impl.IsValid()) {
        return;
    }
    Impl->ActorManager.SetPoolEvictionSettings(ActorClass, Settings);
    Impl->EvictionScheduler.SyncSchedule();
}

/**
 * @brief 查询指定 Actor 类别的闲置弹性缩容与淘汰策略
 * @param ActorClass 目标 Actor 类
 * @return 当前淘汰配置
 */
FPoolEvictionSettings UObjectPoolSubsystem::GetActorPoolEvictionSettings(TSubclassOf<AActor> ActorClass) const {
    return Impl.IsValid() ? Impl->ActorManager.GetPoolEvictionSettings(ActorClass) : FPoolEvictionSettings();
}

/**
 * @brief 配置指定纯 UObject 类别的闲置弹性缩容与淘汰策略
 * @param ObjectClass 目标 UObject 类
 * @param Settings 淘汰策略配置
 */
void UObjectPoolSubsystem::SetObjectPoolEvictionSettings(TSubclassOf<UObject> ObjectClass, const FPoolEvictionSettings& Settings) {
    if (!Impl.IsValid()) {
        return;
    }
    Impl->ObjectManager.SetPoolEvictionSettings(ObjectClass, Settings);
    Impl->EvictionScheduler.SyncSchedule();
}

/**
 * @brief 查询指定纯 UObject 类的闲置弹性缩容与淘汰策略
 * @param ObjectClass 目标 UObject 类
 * @return 当前淘汰配置
 */
FPoolEvictionSettings UObjectPoolSubsystem::GetObjectPoolEvictionSettings(TSubclassOf<UObject> ObjectClass) const {
    return Impl.IsValid() ? Impl->ObjectManager.GetPoolEvictionSettings(ObjectClass) : FPoolEvictionSettings();
}

/**
 * @brief 强制触发一次全部池的超时冷实例淘汰
 * @param MaxEvictionsPerStep 单次最大总销毁配额
 * @param CustomNowSeconds 自定义时间戳
 * @return 实际销毁的冷实例总数
 */
int32 UObjectPoolSubsystem::ForceEvictExpired(const int32 MaxEvictionsPerStep, const double CustomNowSeconds) {
    if (!Impl.IsValid()) {
        return 0;
    }
    return Impl->EvictionScheduler.ForceEvictExpired(MaxEvictionsPerStep, CustomNowSeconds);
}

/**
 * @brief 获取当前对象池运行时的全局遥测快照
 * @return 全局遥测快照结构体
 */
FPoolTelemetrySnapshot UObjectPoolSubsystem::GetTelemetrySnapshot() const {
    FPoolTelemetrySnapshot Snapshot;
    if (!Impl.IsValid()) {
        return Snapshot;
    }
    Snapshot.CacheHits = Impl->ActorManager.GetCacheHits() + Impl->ObjectManager.GetCacheHits();
    Snapshot.CacheMisses = Impl->ActorManager.GetCacheMisses() + Impl->ObjectManager.GetCacheMisses();
    Snapshot.TotalAcquires = Impl->ActorManager.GetTotalAcquires() + Impl->ObjectManager.GetTotalAcquires();
    Snapshot.TotalActiveCount = Impl->ActorManager.GetTotalActiveCount() + Impl->ObjectManager.GetTotalActiveCount();
    Snapshot.TotalIdleCount = Impl->ActorManager.GetTotalIdleCount() + Impl->ObjectManager.GetTotalIdleCount();
    return Snapshot;
}

/**
 * @brief 重置对象池遥测统计计数器
 */
void UObjectPoolSubsystem::ResetTelemetry() {
    if (Impl.IsValid()) {
        Impl->ActorManager.ResetTelemetry();
        Impl->ObjectManager.ResetTelemetry();
    }
}

/**
 * @brief 强制释放当前所有池中的闲置空闲实例
 * @param bPreserveMinCount 是否保留各池配置的 MinPreservedCount 保底实例数
 * @return 实际销毁并释放的空闲实例总数
 */
int32 UObjectPoolSubsystem::FlushAllIdleInstances(const bool bPreserveMinCount) {
    if (!Impl.IsValid()) {
        return 0;
    }
    return Impl->ResilienceController.FlushAllIdleInstances(bPreserveMinCount);
}

/**
 * @brief 设置是否启用在操作系统/引擎内存紧张时自动紧急泄洪缩容
 * @param bEnable true 为开启
 */
void UObjectPoolSubsystem::SetLowMemoryAutoFlushEnabled(const bool bEnable) {
    if (Impl.IsValid()) {
        Impl->ResilienceController.SetLowMemoryAutoFlushEnabled(bEnable);
    }
}

/**
 * @brief 查询当前低内存自动泄洪保护是否开启
 * @return 若开启返回 true
 */
bool UObjectPoolSubsystem::IsLowMemoryAutoFlushEnabled() const noexcept {
    return Impl.IsValid() && Impl->ResilienceController.IsLowMemoryAutoFlushEnabled();
}

/**
 * @brief 获取世界上下文绑定的对象池子系统单例引用
 * @param WorldContextObject 世界上下文对象
 * @return 子系统实例引用
 */
UObjectPoolSubsystem& UObjectPoolSubsystem::Get(const UObject* WorldContextObject) {
    UObjectPoolSubsystem* Subsystem = GetPtr(WorldContextObject);
    checkf(Subsystem != nullptr, TEXT("UObjectPoolSubsystem::Get called with invalid WorldContextObject"));
    return *Subsystem;
}

/**
 * @brief 安全获取世界上下文绑定的对象池子系统指针
 * @param WorldContextObject 世界上下文对象
 * @return 子系统实例指针，上下文无效返回 nullptr
 */
UObjectPoolSubsystem* UObjectPoolSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr || GEngine == nullptr) {
        return nullptr;
    }
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
        return World->GetSubsystem<UObjectPoolSubsystem>();
    }
    return nullptr;
}

/**
 * @brief UE 垃圾回收强引用收集静态入口
 * @param InThis 子系统对象指针
 * @param Collector 引用收集器
 */
void UObjectPoolSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector) {
    UObjectPoolSubsystem* This = CastChecked<UObjectPoolSubsystem>(InThis);
    if (This->Impl.IsValid()) {
        This->Impl->ActorManager.AddReferencedObjects(Collector, This);
        This->Impl->ObjectManager.AddReferencedObjects(Collector, This);
    }
    Super::AddReferencedObjects(InThis, Collector);
}

/**
 * @brief 监听已借出 Actor 被外部强制 Destroy 时的清理委托
 * @param DestroyedActor 被外部销毁的目标 Actor
 */
void UObjectPoolSubsystem::OnPooledActorDestroyed(AActor* DestroyedActor) {
    if (Impl.IsValid()) {
        Impl->ActorManager.OnPooledActorDestroyed(DestroyedActor);
    }
}

/**
 * @brief 内部借出或生成 Actor 实例核心转发实现
 * @param ActorClass 目标 Actor 类
 * @param SpawnTransform 初始空间变换
 * @param Payload 可选业务载荷视图
 * @return 成功返回 Actor 指针，失败返回 Failure
 */
TGameStatusResult<AActor*> UObjectPoolSubsystem::AcquireActorInternal(
    TSubclassOf<AActor> ActorClass,
    const FTransform& SpawnTransform,
    const FPoolAcquirePayload Payload
) {
    if (!Impl.IsValid()) {
        return TGameStatusResult<AActor*>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidContext, TEXT("UObjectPoolSubsystem is not initialized"))
        );
    }
    return Impl->ActorManager.AcquireActor(ActorClass, SpawnTransform, Payload);
}

/**
 * @brief 内部借出或构建非 Actor UObject 实例核心转发实现
 * @param ObjectClass 目标 UObject 派生类
 * @param Payload 可选业务载荷视图
 * @return 成功返回 UObject 指针，失败返回 Failure
 */
TGameStatusResult<UObject*> UObjectPoolSubsystem::AcquireObjectInternal(
    TSubclassOf<UObject> ObjectClass,
    const FPoolAcquirePayload Payload
) {
    if (!Impl.IsValid()) {
        return TGameStatusResult<UObject*>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidContext, TEXT("UObjectPoolSubsystem is not initialized"))
        );
    }
    return Impl->ObjectManager.AcquireObject(ObjectClass, Payload);
}

/**
 * @brief 内部查询指定对象当前借出世代序号
 * @param Object 目标对象指针
 * @return 当前世代代数
 */
uint32 UObjectPoolSubsystem::FindObjectGeneration(const UObject* Object) const {
    return Impl.IsValid() ? Impl->GenerationTracker.FindGeneration(Object) : 0u;
}

/**
 * @brief 内部校验句柄弱引用与世代代数是否一致
 * @param ObjectWeak 对象弱引用
 * @param ExpectedGeneration 预期借出世代代数
 * @return 活跃且代数匹配返回 true
 */
bool UObjectPoolSubsystem::ValidateHandleGeneration(
    const FWeakObjectPtr& ObjectWeak,
    const uint32 ExpectedGeneration
) const {
    return Impl.IsValid() && Impl->GenerationTracker.ValidateGeneration(ObjectWeak, ExpectedGeneration);
}
