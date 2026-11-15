// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystem/ObjectPoolSubsystem.h"
#include "Engine/World.h"

/**
 * @brief 初始化对象池子系统，设置 Actor 与 UObject 池管理器上下文及调度器
 * @param Collection 子系统集合
 */
void UObjectPoolSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    UWorld* World = GetWorld();
    ActorManager.Initialize(World, &GenerationTracker, this);
    ObjectManager.Initialize(this, &GenerationTracker);
    EvictionScheduler.Initialize(World, &ActorManager, &ObjectManager);
    ResilienceController.Initialize(&ActorManager, &ObjectManager);
}

/**
 * @brief 反初始化对象池子系统，清空所有池中对象与调度器
 */
void UObjectPoolSubsystem::Deinitialize() {
    ResilienceController.Deinitialize();
    EvictionScheduler.Deinitialize();
    DeferredDispatcher.Reset();
    ClearAllPools();
    GenerationTracker.Reset();
    Super::Deinitialize();
}

/**
 * @brief 同步预热指定数量的 Actor 到对象池
 * @param ActorClass 目标 Actor 类
 * @param PrewarmCount 预热生成的实例数量
 */
void UObjectPoolSubsystem::Prewarm(TSubclassOf<AActor> ActorClass, const int32 PrewarmCount) {
    ActorManager.Prewarm(ActorClass, PrewarmCount);
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
    return FPoolAsyncLoader::PrewarmActorsAsync(this, ActorManager, ActorClass, PrewarmCount, MaxSpawnsPerStep, CancellationToken);
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
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    return FPoolAsyncLoader::PrewarmActorsByPathAsync(this, ActorManager, ActorClassPath, PrewarmCount, MaxSpawnsPerStep, CancellationToken);
}

/**
 * @brief 设置指定 Actor 类别的最大空闲实例缓存容量
 * @param ActorClass 目标 Actor 类
 * @param InMaxIdleCount 最大空闲数
 */
void UObjectPoolSubsystem::SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, const int32 InMaxIdleCount) {
    ActorManager.SetMaxPoolCapacity(ActorClass, InMaxIdleCount);
}

/**
 * @brief 配置指定 Actor 类别归还入池时的组件自动重置策略掩码
 * @param ActorClass 目标 Actor 类
 * @param InResetFlags 重置位掩码
 */
void UObjectPoolSubsystem::SetPoolResetFlags(TSubclassOf<AActor> ActorClass, const EPoolResetFlags InResetFlags) {
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
 * @return 借出的 Actor 指针
 */
AActor* UObjectPoolSubsystem::K2_AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform) {
    const TGameStatusResult<AActor*> Result = AcquireActor<AActor>(ActorClass, SpawnTransform);
    return Result.IsSuccess() ? Result.GetValue() : nullptr;
}

/**
 * @brief 内部借出或生成 Actor 实例核心实现
 * @param ActorClass 目标 Actor 类
 * @param SpawnTransform 初始空间变换
 * @return 成功返回 Actor 指针，失败返回 Failure
 */
TGameStatusResult<AActor*> UObjectPoolSubsystem::AcquireActorInternal(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform) {
    return ActorManager.AcquireActor(ActorClass, SpawnTransform);
}

/**
 * @brief 将已借出的 Actor 归还回池缓存（蓝图轻量门面）
 * @param Actor 待归还的 Actor 实例
 */
void UObjectPoolSubsystem::K2_ReleaseActor(AActor* Actor) {
    ReleaseActor(Actor);
}

/**
 * @brief 将已借出的 Actor 归还回池缓存（强类型契约）
 * @param Actor 待归还的 Actor 实例
 * @return 成功返回 Success，失败返回 Failure
 */
TGameStatusResult<void> UObjectPoolSubsystem::ReleaseActor(AActor* Actor) {
    return ActorManager.ReleaseActor(Actor);
}

/**
 * @brief 异步延时将 Actor 加入归还队列
 * @param Actor 待归还的目标 Actor 实例
 * @return 成功入队返回 Success，队列已满或 Actor 无效返回 Failure
 */
TGameStatusResult<void> UObjectPoolSubsystem::ReleaseActorDeferred(AActor* Actor) {
    return DeferredDispatcher.EnqueueDeferredRelease(Actor);
}

/**
 * @brief 处理延时归还队列中的 Actor
 * @param MaxProcessCount 本次最大处理配额
 * @return 实际成功归还入池的 Actor 数量
 */
int32 UObjectPoolSubsystem::ProcessDeferredReleases(const int32 MaxProcessCount) {
    return DeferredDispatcher.ProcessDeferredReleases(ActorManager, MaxProcessCount);
}

/**
 * @brief 查询指定 Actor 类别的池容量与活跃统计快照
 * @param ActorClass 目标类
 * @return 池状态统计结构体
 */
FPoolStats UObjectPoolSubsystem::GetPoolStats(TSubclassOf<AActor> ActorClass) const {
    return ActorManager.GetPoolStats(ActorClass);
}

/**
 * @brief 清空指定 Actor 类别的所有空闲实例并释放资源
 * @param ActorClass 目标 Actor 类
 */
void UObjectPoolSubsystem::ClearPool(TSubclassOf<AActor> ActorClass) {
    ActorManager.ClearPool(ActorClass);
}

/**
 * @brief 清空所有 Actor 类别池中的全部空闲实例
 */
void UObjectPoolSubsystem::ClearAllActorPools() {
    ActorManager.ClearAllPools();
}

/**
 * @brief 清空所有池类别（Actor 与纯 UObject）中的全部空闲实例
 */
void UObjectPoolSubsystem::ClearAllPools() {
    ClearAllActorPools();
    ClearAllObjectPools();
}

/**
 * @brief 同步预热指定数量的纯 UObject 到对象池
 * @param ObjectClass 目标 UObject 类
 * @param PrewarmCount 预热生成的实例数量
 */
void UObjectPoolSubsystem::PrewarmObjectPool(TSubclassOf<UObject> ObjectClass, const int32 PrewarmCount) {
    ObjectManager.Prewarm(ObjectClass, PrewarmCount);
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
    return FPoolAsyncLoader::PrewarmObjectsAsync(this, ObjectManager, ObjectClass, PrewarmCount, MaxSpawnsPerStep, CancellationToken);
}

/**
 * @brief 基于软引用的 C++20 协程流式异步预热纯 UObject 对象池
 * @param ObjectClassPath 目标 UObject 类的软对象路径
 * @param PrewarmCount 预热生成的总空闲实例数
 * @param MaxSpawnsPerStep 单步最大生成实例数
 * @param CancellationToken 协作式取消令牌
 * @return 异步协程任务，返回实际成功实例化的数量
 */
TGameCoroTask<int32> UObjectPoolSubsystem::PrewarmObjectPoolByPathAsync(
    const FSoftObjectPath& ObjectClassPath,
    const int32 PrewarmCount,
    const int32 MaxSpawnsPerStep,
    FCancellationToken CancellationToken
) {
    return FPoolAsyncLoader::PrewarmObjectsByPathAsync(this, ObjectManager, ObjectClassPath, PrewarmCount, MaxSpawnsPerStep, CancellationToken);
}

/**
 * @brief 设置指定非 Actor UObject 类别的最大空闲缓存容量
 * @param ObjectClass 目标 UObject 派生类
 * @param InMaxIdleCount 最大空闲数
 */
void UObjectPoolSubsystem::SetMaxObjectPoolCapacity(TSubclassOf<UObject> ObjectClass, const int32 InMaxIdleCount) {
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
 * @return 成功返回 Success，失败返回 Failure
 */
TGameStatusResult<void> UObjectPoolSubsystem::ReleaseObject(UObject* Object) {
    return ObjectManager.ReleaseObject(Object);
}

/**
 * @brief 查询指定纯 UObject 类的池容量与活跃统计快照
 * @param ObjectClass 目标类
 * @return 池状态统计结构体
 */
FPoolStats UObjectPoolSubsystem::GetObjectPoolStats(TSubclassOf<UObject> ObjectClass) const {
    return ObjectManager.GetPoolStats(ObjectClass);
}

/**
 * @brief 清空指定纯 UObject 类别的所有空闲实例并释放垃圾回收引用
 * @param ObjectClass 目标类
 */
void UObjectPoolSubsystem::ClearObjectPool(TSubclassOf<UObject> ObjectClass) {
    ObjectManager.ClearPool(ObjectClass);
}

/**
 * @brief 清空所有非 Actor 纯 UObject 类别池中的全部空闲实例
 */
void UObjectPoolSubsystem::ClearAllObjectPools() {
    ObjectManager.ClearAllPools();
}

/**
 * @brief 配置指定 Actor 类别的闲置弹性缩容与淘汰策略
 * @param ActorClass 目标 Actor 类
 * @param Settings 淘汰策略配置
 */
void UObjectPoolSubsystem::SetActorPoolEvictionSettings(TSubclassOf<AActor> ActorClass, const FPoolEvictionSettings& Settings) {
    ActorManager.SetPoolEvictionSettings(ActorClass, Settings);
    if (Settings.bEnableEviction && !EvictionScheduler.IsTimerActive()) {
        EvictionScheduler.StartEvictionTimer(Settings.CheckIntervalSeconds);
    }
}

/**
 * @brief 查询指定 Actor 类别的闲置弹性缩容与淘汰策略
 * @param ActorClass 目标 Actor 类
 * @return 当前淘汰配置
 */
FPoolEvictionSettings UObjectPoolSubsystem::GetActorPoolEvictionSettings(TSubclassOf<AActor> ActorClass) const {
    return ActorManager.GetPoolEvictionSettings(ActorClass);
}

/**
 * @brief 配置指定纯 UObject 类别的闲置弹性缩容与淘汰策略
 * @param ObjectClass 目标 UObject 类
 * @param Settings 淘汰策略配置
 */
void UObjectPoolSubsystem::SetObjectPoolEvictionSettings(TSubclassOf<UObject> ObjectClass, const FPoolEvictionSettings& Settings) {
    ObjectManager.SetPoolEvictionSettings(ObjectClass, Settings);
    if (Settings.bEnableEviction && !EvictionScheduler.IsTimerActive()) {
        EvictionScheduler.StartEvictionTimer(Settings.CheckIntervalSeconds);
    }
}

/**
 * @brief 查询指定纯 UObject 类的闲置弹性缩容与淘汰策略
 * @param ObjectClass 目标 UObject 类
 * @return 当前淘汰配置
 */
FPoolEvictionSettings UObjectPoolSubsystem::GetObjectPoolEvictionSettings(TSubclassOf<UObject> ObjectClass) const {
    return ObjectManager.GetPoolEvictionSettings(ObjectClass);
}

/**
 * @brief 强制触发一次全部池的超时冷实例淘汰
 * @param MaxEvictionsPerStep 单次最大销毁配额
 * @param CustomNowSeconds 自定义时间戳
 * @return 实际销毁的冷实例总数
 */
int32 UObjectPoolSubsystem::ForceEvictExpired(const int32 MaxEvictionsPerStep, const double CustomNowSeconds) {
    return EvictionScheduler.ForceEvictExpired(MaxEvictionsPerStep, CustomNowSeconds);
}

/**
 * @brief 获取当前对象池运行时的全景遥测快照
 * @return 全景遥测快照结构体
 */
FPoolTelemetrySnapshot UObjectPoolSubsystem::GetTelemetrySnapshot() const {
    FPoolTelemetrySnapshot Snapshot;
    Snapshot.CacheHits = ActorManager.GetCacheHits() + ObjectManager.GetCacheHits();
    Snapshot.CacheMisses = ActorManager.GetCacheMisses() + ObjectManager.GetCacheMisses();
    Snapshot.TotalAcquires = ActorManager.GetTotalAcquires() + ObjectManager.GetTotalAcquires();
    Snapshot.TotalActiveCount = ActorManager.GetTotalActiveCount() + ObjectManager.GetTotalActiveCount();
    Snapshot.TotalIdleCount = ActorManager.GetTotalIdleCount() + ObjectManager.GetTotalIdleCount();
    return Snapshot;
}

/**
 * @brief 重置对象池遥测统计计数器
 */
void UObjectPoolSubsystem::ResetTelemetry() {
    ActorManager.ResetTelemetry();
    ObjectManager.ResetTelemetry();
}

/**
 * @brief 强制释放当前所有池中的全部闲置空闲实例
 * @param bPreserveMinCount 是否保留各池配置的最小保底实例数
 * @return 实际销毁并释放的空闲实例总数
 */
int32 UObjectPoolSubsystem::FlushAllIdleInstances(const bool bPreserveMinCount) {
    return ResilienceController.FlushAllIdleInstances(bPreserveMinCount);
}

/**
 * @brief 设置是否启用在操作系统/引擎内存紧张时自动紧急泄洪缩容
 * @param bEnable true 为开启（默认开启）
 */
void UObjectPoolSubsystem::SetLowMemoryAutoFlushEnabled(const bool bEnable) {
    ResilienceController.SetLowMemoryAutoFlushEnabled(bEnable);
}

/**
 * @brief 查询当前低内存自动泄洪保护是否开启
 * @return 若开启返回 true
 */
bool UObjectPoolSubsystem::IsLowMemoryAutoFlushEnabled() const noexcept {
    return ResilienceController.IsLowMemoryAutoFlushEnabled();
}

/**
 * @brief 响应操作系统或引擎低内存修剪警报的回调
 */
void UObjectPoolSubsystem::OnMemoryTrim() {
    ResilienceController.OnMemoryTrim();
}

/**
 * @brief 监听已借出 Actor 被外部强制 Destroy 时的清理委托
 * @param DestroyedActor 被外部销毁的目标 Actor
 */
void UObjectPoolSubsystem::OnPooledActorDestroyed(AActor* DestroyedActor) {
    ActorManager.OnPooledActorDestroyed(DestroyedActor);
}

/**
 * @brief 向空闲池中填充一个预热生成的 Actor
 * @param ActorClass 目标 Actor 类
 * @return 成功生成入池返回 Success，参数无效或生成失败返回 Failure
 */
TGameStatusResult<void> UObjectPoolSubsystem::SpawnIdleActorToPool(TSubclassOf<AActor> ActorClass) {
    return ActorManager.SpawnIdleActorToPool(ActorClass);
}
