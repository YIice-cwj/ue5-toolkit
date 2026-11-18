// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/IGamePoolable.h"
#include "Core/ObjectPoolTypes.h"
#include "Handle/PoolGenerationTracker.h"
#include "Handle/PooledObjectHandle.h"
#include "Managers/ActorPoolManager.h"
#include "Managers/UObjectPoolManager.h"
#include "Scheduling/PoolDeferredDispatcher.h"
#include "Scheduling/PoolEvictionScheduler.h"
#include "Scheduling/PoolAsyncLoader.h"
#include "Resilience/PoolResilienceController.h"
#include "Telemetry/ObjectPoolStats.h"
#include "Types/TGameResult.h"
#include "Subsystems/WorldSubsystem.h"
#include "Async/FCancellationToken.h"
#include "Async/TGameCoroTask.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPath.h"

#include "ObjectPoolSubsystem.generated.h"

/**
 * @brief 对象池子系统（World 级别单例外观门面，组合底层容器、调度器与弹性控制器）
 */
UCLASS()
class GAMEPOOL_API UObjectPoolSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
    /**
     * @brief 初始化对象池子系统，设置 Actor 与 UObject 池管理器上下文及调度器
     * @param Collection 子系统集合引用
     */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 反初始化对象池子系统，清空所有池中对象、调度器与控制器
     */
    virtual void Deinitialize() override;

    /**
     * @brief 同步预热指定数量的 Actor 到对象池
     * @param ActorClass 目标 Actor 类
     * @param PrewarmCount 预热生成的实例数量
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount);

    /**
     * @brief 基于 C++20 协程的分步异步预热 Actor
     * @param ActorClass 目标 Actor 类
     * @param PrewarmCount 预热生成的空闲实例总数量
     * @param MaxSpawnsPerStep 单步最大生成实例数（防帧率尖刺）
     * @param CancellationToken 协作式取消令牌
     * @return 实际成功预热生成的实例总数异步协程任务
     */
    TGameCoroTask<int32> PrewarmAsync(
        TSubclassOf<AActor> ActorClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 基于软对象路径的 C++20 协程流式异步预热
     * @param ActorClassPath 目标 Actor 类的软对象路径
     * @param PrewarmCount 预热生成的空闲实例总数量
     * @param MaxSpawnsPerStep 单步最大生成实例数
     * @param CancellationToken 协作式取消令牌
     * @return 实际成功预热生成的实例总数异步协程任务
     */
    TGameCoroTask<int32> PrewarmAsync(
        const FSoftObjectPath& ActorClassPath,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 设置指定 Actor 类别的最大空闲实例缓存容量
     * @param ActorClass 目标 Actor 类
     * @param InMaxIdleCount 最大空闲数（<=0 表示无限制）
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, int32 InMaxIdleCount);

    /**
     * @brief 配置指定 Actor 类别归还入池时的组件自动重置策略掩码
     * @param ActorClass 目标 Actor 类
     * @param InResetFlags 重置位掩码
     */
    void SetPoolResetFlags(TSubclassOf<AActor> ActorClass, EPoolResetFlags InResetFlags);

    /**
     * @brief 查询指定 Actor 类别当前的组件自动重置策略掩码
     * @param ActorClass 目标 Actor 类
     * @return 当前配置的组件重置策略掩码
     */
    [[nodiscard]] EPoolResetFlags GetPoolResetFlags(TSubclassOf<AActor> ActorClass) const;

    /**
     * @brief 从池中借出或生成 Actor 实例（蓝图专用外观方法）
     * @param ActorClass 目标 Actor 类
     * @param SpawnTransform 初始空间变换
     * @return 借出的 Actor 原生指针，失败返回 nullptr
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Acquire Actor", DeterminesOutputType = "ActorClass"))
    AActor* K2_AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform);

    /**
     * @brief 从池中借出或生成强类型 Actor 实例
     * @tparam T 具体的 AActor 派生类
     * @param ActorClass 目标 Actor 类
     * @param SpawnTransform 初始空间变换
     * @return 包含目标 Actor 指针的强类型状态结果
     */
    template <typename T = AActor>
    TGameStatusResult<T*> AcquireActor(
        TSubclassOf<AActor> ActorClass = T::StaticClass(),
        const FTransform& SpawnTransform = FTransform::Identity
    );

    /**
     * @brief 从池中借出 Actor 实例并传递业务载荷
     * @tparam T 具体的 AActor 派生类
     * @tparam TPayload 业务载荷类型
     * @param ActorClass 目标 Actor 类
     * @param SpawnTransform 初始空间变换
     * @param Payload 业务载荷引用
     * @return 包含目标 Actor 指针的强类型状态结果
     */
    template <typename T = AActor, typename TPayload>
    TGameStatusResult<T*> AcquireActorWithPayload(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        const TPayload& Payload
    );

    /**
     * @brief 从池中借出 Actor 并包装为带代数防悬空的安全句柄
     * @tparam T 具体的 AActor 派生类
     * @param ActorClass 目标 Actor 类
     * @param SpawnTransform 初始空间变换
     * @return 包含安全句柄的强类型状态结果
     */
    template <typename T = AActor>
    TGameStatusResult<TPooledObjectHandle<T>> AcquireActorHandle(
        TSubclassOf<AActor> ActorClass = T::StaticClass(),
        const FTransform& SpawnTransform = FTransform::Identity
    );

    /**
     * @brief 从池中借出 Actor 并传递载荷，包装为带代数防悬空的安全句柄
     * @tparam T 具体的 AActor 派生类
     * @tparam TPayload 业务载荷类型
     * @param ActorClass 目标 Actor 类
     * @param SpawnTransform 初始空间变换
     * @param Payload 业务载荷引用
     * @return 包含安全句柄的强类型状态结果
     */
    template <typename T = AActor, typename TPayload>
    TGameStatusResult<TPooledObjectHandle<T>> AcquireActorHandleWithPayload(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        const TPayload& Payload
    );

    /**
     * @brief 借出 RAII 作用域自动归还租约卫士
     * @tparam T 具体的 AActor 派生类
     * @param ActorClass 目标 Actor 类
     * @param SpawnTransform 初始空间变换
     * @return 包含作用域租约卫士的强类型状态结果
     */
    template <typename T = AActor>
    TGameStatusResult<TScopedPoolRental<T>> AcquireScopedActor(
        TSubclassOf<AActor> ActorClass = T::StaticClass(),
        const FTransform& SpawnTransform = FTransform::Identity
    );

    /**
     * @brief 将已借出的 Actor 归还回池缓存（蓝图专用外观方法）
     * @param Actor 待归还的 Actor 实例
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Release Actor"))
    void K2_ReleaseActor(AActor* Actor);

    /**
     * @brief 将已借出的 Actor 归还回池缓存
     * @param Actor 待归还的 Actor 实例
     * @return 归还状态结果
     */
    TGameStatusResult<void> ReleaseActor(AActor* Actor);

    /**
     * @brief 通过安全句柄归还 Actor 回池
     * @tparam T 具体的 AActor 派生类
     * @param Handle 待归还的受管句柄引用
     * @return 归还状态结果
     */
    template <typename T>
    TGameStatusResult<void> ReleaseActor(const TPooledObjectHandle<T>& Handle) {
        if (!Handle.IsValid()) {
            return TGameStatusResult<void>::Failure(5002, TEXT("句柄无效或已过期"));
        }
        return ReleaseActor(Handle.Get());
    }

    /**
     * @brief 异步延时将 Actor 加入归还队列（并发/多线程安全）
     * @param Actor 待归还的目标 Actor 实例
     * @return 成功入队返回 Success，队列已满或 Actor 无效返回 Failure
     */
    TGameStatusResult<void> ReleaseActorDeferred(AActor* Actor);

    /**
     * @brief 处理延时归还队列中的待归还实例（Actor 与 UObject）
     * @param MaxProcessCount 本次单步最大处理配额
     * @return 实际成功归还入池的实例总数
     */
    int32 ProcessDeferredReleases(int32 MaxProcessCount = 64);

    /**
     * @brief 查询指定 Actor 类别的池容量与活跃统计快照
     * @param ActorClass 目标 Actor 类
     * @return 池状态统计结构体
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|ObjectPool")
    FPoolStats GetPoolStats(TSubclassOf<AActor> ActorClass) const;

    /**
     * @brief 清空指定 Actor 类别的所有空闲实例并释放资源
     * @param ActorClass 目标 Actor 类
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearPool(TSubclassOf<AActor> ActorClass);

    /**
     * @brief 清空所有 Actor 类别池中的全部空闲实例
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearAllActorPools();

    /**
     * @brief 清空所有池类别（Actor 与纯 UObject）中的全部空闲实例
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearAllPools();

    /**
     * @brief 同步预热指定数量的纯 UObject 到对象池
     * @param ObjectClass 目标 UObject 类
     * @param PrewarmCount 预热生成的实例数量
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void PrewarmObjectPool(TSubclassOf<UObject> ObjectClass, int32 PrewarmCount);

    /**
     * @brief 基于 C++20 协程的分步异步预热纯 UObject 对象池
     * @param ObjectClass 目标 UObject 类
     * @param PrewarmCount 预热生成的总空闲实例数
     * @param MaxSpawnsPerStep 单步最大生成实例数
     * @param CancellationToken 协作式取消令牌
     * @return 异步协程任务，返回实际成功实例化的数量
     */
    TGameCoroTask<int32> PrewarmObjectPoolAsync(
        TSubclassOf<UObject> ObjectClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 4,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 基于软引用的 C++20 协程流式异步预热纯 UObject 对象池
     * @param ObjectClassPath 目标 UObject 类的软对象路径
     * @param PrewarmCount 预热生成的总空闲实例数
     * @param MaxSpawnsPerStep 单步最大生成实例数
     * @param CancellationToken 协作式取消令牌
     * @return 异步协程任务，返回实际成功实例化的数量
     */
    TGameCoroTask<int32> PrewarmObjectPoolByPathAsync(
        const FSoftObjectPath& ObjectClassPath,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 4,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 设置指定非 Actor UObject 类别的最大空闲缓存容量
     * @param ObjectClass 目标 UObject 派生类
     * @param InMaxIdleCount 最大空闲数（<=0 表示无限制）
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void SetMaxObjectPoolCapacity(TSubclassOf<UObject> ObjectClass, int32 InMaxIdleCount);

    /**
     * @brief 从池中借出或构建非 Actor UObject 实例（蓝图专用外观方法）
     * @param ObjectClass 目标 UObject 派生类
     * @return 激活的 UObject 实例指针，失败返回 nullptr
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Acquire Object", DeterminesOutputType = "ObjectClass"))
    UObject* K2_AcquireObject(TSubclassOf<UObject> ObjectClass);

    /**
     * @brief 从池中借出或构建强类型非 Actor UObject 实例
     * @tparam T 具体的 UObject 派生类
     * @param ObjectClass 目标类
     * @return 包含对象指针的强类型状态结果
     */
    template <typename T = UObject>
    TGameStatusResult<T*> AcquireObject(TSubclassOf<UObject> ObjectClass = T::StaticClass());

    /**
     * @brief 从池中借出纯 UObject 实例并传递业务载荷
     * @tparam T 具体的 UObject 派生类
     * @tparam TPayload 业务载荷类型
     * @param ObjectClass 目标类
     * @param Payload 业务载荷引用
     * @return 包含对象指针的强类型状态结果
     */
    template <typename T = UObject, typename TPayload>
    TGameStatusResult<T*> AcquireObjectWithPayload(
        TSubclassOf<UObject> ObjectClass,
        const TPayload& Payload
    );

    /**
     * @brief 从池中借出纯 UObject 并包装为带代数防悬空的安全句柄
     * @tparam T 具体的 UObject 派生类
     * @param ObjectClass 目标类
     * @return 包含安全句柄的强类型状态结果
     */
    template <typename T = UObject>
    TGameStatusResult<TPooledObjectHandle<T>> AcquireObjectHandle(
        TSubclassOf<UObject> ObjectClass = T::StaticClass()
    );

    /**
     * @brief 从池中借出纯 UObject 并传递载荷，包装为带代数防悬空的安全句柄
     * @tparam T 具体的 UObject 派生类
     * @tparam TPayload 业务载荷类型
     * @param ObjectClass 目标类
     * @param Payload 业务载荷引用
     * @return 包含安全句柄的强类型状态结果
     */
    template <typename T = UObject, typename TPayload>
    TGameStatusResult<TPooledObjectHandle<T>> AcquireObjectHandleWithPayload(
        TSubclassOf<UObject> ObjectClass,
        const TPayload& Payload
    );

    /**
     * @brief 借出纯 UObject 的 RAII 作用域自动归还租约卫士
     * @tparam T 具体的 UObject 派生类
     * @param ObjectClass 目标类
     * @return 包含作用域租约卫士的强类型状态结果
     */
    template <typename T = UObject>
    TGameStatusResult<TScopedPoolRental<T>> AcquireScopedObject(
        TSubclassOf<UObject> ObjectClass = T::StaticClass()
    );

    /**
     * @brief 将已借出的非 Actor UObject 归还回池缓存（蓝图专用外观方法）
     * @param Object 待归还的 UObject 实例
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Release Object"))
    void K2_ReleaseObject(UObject* Object);

    /**
     * @brief 将已借出的非 Actor UObject 归还回池缓存
     * @param Object 待归还的 UObject 实例
     * @return 归还状态结果
     */
    TGameStatusResult<void> ReleaseObject(UObject* Object);

    /**
     * @brief 通过安全句柄归还非 Actor UObject 回池
     * @tparam T 具体的 UObject 派生类
     * @param Handle 待归还的受管句柄引用
     * @return 归还状态结果
     */
    template <typename T>
    TGameStatusResult<void> ReleaseObject(const TPooledObjectHandle<T>& Handle) {
        if (!Handle.IsValid()) {
            return TGameStatusResult<void>::Failure(5002, TEXT("句柄无效或已过期"));
        }
        return ReleaseObject(Handle.Get());
    }

    /**
     * @brief 异步延时将纯 UObject 加入归还队列（并发/多线程安全）
     * @param Object 待归还的目标 UObject 实例
     * @return 成功入队返回 Success，队列已满或 Object 无效返回 Failure
     */
    TGameStatusResult<void> ReleaseObjectDeferred(UObject* Object);

    /**
     * @brief 查询指定纯 UObject 类的池容量与活跃统计快照
     * @param ObjectClass 目标类
     * @return 池状态统计结构体
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|ObjectPool")
    FPoolStats GetObjectPoolStats(TSubclassOf<UObject> ObjectClass) const;

    /**
     * @brief 清空指定纯 UObject 类别的所有空闲实例并释放垃圾回收引用
     * @param ObjectClass 目标类
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearObjectPool(TSubclassOf<UObject> ObjectClass);

    /**
     * @brief 清空所有非 Actor 纯 UObject 类别池中的全部空闲实例
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearAllObjectPools();

    /**
     * @brief 配置指定 Actor 类别的闲置弹性缩容与淘汰策略
     * @param ActorClass 目标 Actor 类
     * @param Settings 淘汰策略配置
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|Eviction")
    void SetActorPoolEvictionSettings(TSubclassOf<AActor> ActorClass, const FPoolEvictionSettings& Settings);

    /**
     * @brief 查询指定 Actor 类别的闲置弹性缩容与淘汰策略
     * @param ActorClass 目标 Actor 类
     * @return 当前淘汰配置
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|Eviction")
    FPoolEvictionSettings GetActorPoolEvictionSettings(TSubclassOf<AActor> ActorClass) const;

    /**
     * @brief 配置指定纯 UObject 类别的闲置弹性缩容与淘汰策略
     * @param ObjectClass 目标 UObject 类
     * @param Settings 淘汰策略配置
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|Eviction")
    void SetObjectPoolEvictionSettings(TSubclassOf<UObject> ObjectClass, const FPoolEvictionSettings& Settings);

    /**
     * @brief 查询指定纯 UObject 类的闲置弹性缩容与淘汰策略
     * @param ObjectClass 目标 UObject 类
     * @return 当前淘汰配置
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|Eviction")
    FPoolEvictionSettings GetObjectPoolEvictionSettings(TSubclassOf<UObject> ObjectClass) const;

    /**
     * @brief 强制触发一次全部池的超时冷实例淘汰
     * @param MaxEvictionsPerStep 单次最大销毁配额
     * @param CustomNowSeconds 自定义时间戳
     * @return 实际销毁的冷实例总数
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|Eviction")
    int32 ForceEvictExpired(int32 MaxEvictionsPerStep = 32, double CustomNowSeconds = 0.0);

    /**
     * @brief 获取当前对象池运行时的全景遥测快照
     * @return 全景遥测快照结构体
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|Telemetry")
    FPoolTelemetrySnapshot GetTelemetrySnapshot() const;

    /**
     * @brief 重置对象池遥测统计计数器
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|Telemetry")
    void ResetTelemetry();

    /**
     * @brief 强制释放当前所有池中的全部闲置空闲实例
     * @param bPreserveMinCount 是否保留各池配置的最小保底实例数
     * @return 实际销毁并释放的空闲实例总数
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|Resilience")
    int32 FlushAllIdleInstances(bool bPreserveMinCount = false);

    /**
     * @brief 设置是否启用在操作系统/引擎内存紧张时自动紧急泄洪缩容
     * @param bEnable true 为开启（默认开启）
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|Resilience")
    void SetLowMemoryAutoFlushEnabled(bool bEnable);

    /**
     * @brief 查询当前低内存自动泄洪保护是否开启
     * @return 若开启返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|Resilience")
    bool IsLowMemoryAutoFlushEnabled() const noexcept;

    /**
     * @brief 响应操作系统或引擎低内存修剪警报的回调
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|Resilience")
    void OnMemoryTrim();

    /**
     * @brief 查询指定 Actor 当前的世代代数
     * @param Actor 目标 Actor 实例
     * @return 当前世代序号
     */
    [[nodiscard]] uint32 GetActorGeneration(const AActor* Actor) const {
        return GenerationTracker.FindGeneration(Actor);
    }

    /**
     * @brief 查询指定 UObject 当前的世代代数
     * @param Object 目标对象实例
     * @return 当前世代序号
     */
    [[nodiscard]] uint32 GetObjectGeneration(const UObject* Object) const {
        return GenerationTracker.FindGeneration(Object);
    }

    /**
     * @brief 校验指定对象的句柄代数是否依然处于有效借出状态
     * @param Object 目标对象实例
     * @param ExpectedGeneration 预期借出世代代数
     * @return 活跃且代数匹配返回 true
     */
    [[nodiscard]] bool ValidateHandleGeneration(const UObject* Object, const uint32 ExpectedGeneration) const {
        return GenerationTracker.ValidateGeneration(Object, ExpectedGeneration);
    }

    /**
     * @brief 监听已借出 Actor 被外部强制 Destroy 时的清理委托
     * @param DestroyedActor 被外部销毁的目标 Actor
     */
    UFUNCTION()
    void OnPooledActorDestroyed(AActor* DestroyedActor);

    /**
     * @brief 向空闲池中填充一个预热生成的 Actor
     * @param ActorClass 目标 Actor 类
     * @return 成功生成入池返回 Success，参数无效或生成失败返回 Failure
     */
    TGameStatusResult<void> SpawnIdleActorToPool(TSubclassOf<AActor> ActorClass);

private:
    TGameStatusResult<AActor*> AcquireActorInternal(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform);
    TGameStatusResult<UObject*> AcquireObjectInternal(TSubclassOf<UObject> ObjectClass);

    FPoolGenerationTracker GenerationTracker;
    FActorPoolManager ActorManager;
    FUObjectPoolManager ObjectManager;
    FPoolDeferredDispatcher DeferredDispatcher;
    FPoolEvictionScheduler EvictionScheduler;
    FPoolResilienceController ResilienceController;
};

template <typename T>
TGameStatusResult<T*> UObjectPoolSubsystem::AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform) {
    const TGameStatusResult<AActor*> RawResult = AcquireActorInternal(ActorClass, SpawnTransform);
    if (RawResult.IsFailure()) {
        return TGameStatusResult<T*>::Failure(RawResult.GetError().ErrorCode, RawResult.GetError().Message);
    }
    T* Casted = Cast<T>(RawResult.GetValue());
    if (Casted == nullptr) {
        return TGameStatusResult<T*>::Failure(5001, TEXT("转换至目标 Actor 类型失败"));
    }
    return TGameStatusResult<T*>::Success(Casted);
}

template <typename T, typename TPayload>
TGameStatusResult<T*> UObjectPoolSubsystem::AcquireActorWithPayload(
    TSubclassOf<AActor> ActorClass,
    const FTransform& SpawnTransform,
    const TPayload& Payload
) {
    const TGameStatusResult<T*> Result = AcquireActor<T>(ActorClass, SpawnTransform);
    if (Result.IsFailure()) {
        return Result;
    }
    if (IGamePoolable* Poolable = Cast<IGamePoolable>(Result.GetValue())) {
        Poolable->OnAcquireFromPoolWithPayload(&Payload);
    }
    return Result;
}

template <typename T>
TGameStatusResult<TPooledObjectHandle<T>> UObjectPoolSubsystem::AcquireActorHandle(
    TSubclassOf<AActor> ActorClass,
    const FTransform& SpawnTransform
) {
    const TGameStatusResult<T*> RawResult = AcquireActor<T>(ActorClass, SpawnTransform);
    if (RawResult.IsFailure()) {
        return TGameStatusResult<TPooledObjectHandle<T>>::Failure(RawResult.GetError().ErrorCode, RawResult.GetError().Message);
    }
    T* Actor = RawResult.GetValue();
    const uint32 Gen = GetActorGeneration(Actor);
    return TGameStatusResult<TPooledObjectHandle<T>>::Success(TPooledObjectHandle<T>(Actor, this, Gen));
}

template <typename T, typename TPayload>
TGameStatusResult<TPooledObjectHandle<T>> UObjectPoolSubsystem::AcquireActorHandleWithPayload(
    TSubclassOf<AActor> ActorClass,
    const FTransform& SpawnTransform,
    const TPayload& Payload
) {
    const TGameStatusResult<T*> RawResult = AcquireActorWithPayload<T, TPayload>(ActorClass, SpawnTransform, Payload);
    if (RawResult.IsFailure()) {
        return TGameStatusResult<TPooledObjectHandle<T>>::Failure(RawResult.GetError().ErrorCode, RawResult.GetError().Message);
    }
    T* Actor = RawResult.GetValue();
    const uint32 Gen = GetActorGeneration(Actor);
    return TGameStatusResult<TPooledObjectHandle<T>>::Success(TPooledObjectHandle<T>(Actor, this, Gen));
}

template <typename T>
TGameStatusResult<TScopedPoolRental<T>> UObjectPoolSubsystem::AcquireScopedActor(
    TSubclassOf<AActor> ActorClass,
    const FTransform& SpawnTransform
) {
    const TGameStatusResult<TPooledObjectHandle<T>> HandleResult = AcquireActorHandle<T>(ActorClass, SpawnTransform);
    if (HandleResult.IsFailure()) {
        return TGameStatusResult<TScopedPoolRental<T>>::Failure(HandleResult.GetError().ErrorCode, HandleResult.GetError().Message);
    }
    return TGameStatusResult<TScopedPoolRental<T>>::Success(TScopedPoolRental<T>(HandleResult.GetValue()));
}

template <typename T>
TGameStatusResult<T*> UObjectPoolSubsystem::AcquireObject(TSubclassOf<UObject> ObjectClass) {
    const TGameStatusResult<UObject*> RawResult = AcquireObjectInternal(ObjectClass);
    if (RawResult.IsFailure()) {
        return TGameStatusResult<T*>::Failure(RawResult.GetError().ErrorCode, RawResult.GetError().Message);
    }
    T* Casted = Cast<T>(RawResult.GetValue());
    if (Casted == nullptr) {
        return TGameStatusResult<T*>::Failure(5001, TEXT("转换至目标 UObject 类型失败"));
    }
    return TGameStatusResult<T*>::Success(Casted);
}

template <typename T, typename TPayload>
TGameStatusResult<T*> UObjectPoolSubsystem::AcquireObjectWithPayload(
    TSubclassOf<UObject> ObjectClass,
    const TPayload& Payload
) {
    const TGameStatusResult<T*> Result = AcquireObject<T>(ObjectClass);
    if (Result.IsFailure()) {
        return Result;
    }
    if (IGamePoolable* Poolable = Cast<IGamePoolable>(Result.GetValue())) {
        Poolable->OnAcquireFromPoolWithPayload(&Payload);
    }
    return Result;
}

template <typename T>
TGameStatusResult<TPooledObjectHandle<T>> UObjectPoolSubsystem::AcquireObjectHandle(
    TSubclassOf<UObject> ObjectClass
) {
    const TGameStatusResult<T*> RawResult = AcquireObject<T>(ObjectClass);
    if (RawResult.IsFailure()) {
        return TGameStatusResult<TPooledObjectHandle<T>>::Failure(RawResult.GetError().ErrorCode, RawResult.GetError().Message);
    }
    T* Object = RawResult.GetValue();
    const uint32 Gen = GetObjectGeneration(Object);
    return TGameStatusResult<TPooledObjectHandle<T>>::Success(TPooledObjectHandle<T>(Object, this, Gen));
}

template <typename T, typename TPayload>
TGameStatusResult<TPooledObjectHandle<T>> UObjectPoolSubsystem::AcquireObjectHandleWithPayload(
    TSubclassOf<UObject> ObjectClass,
    const TPayload& Payload
) {
    const TGameStatusResult<T*> RawResult = AcquireObjectWithPayload<T, TPayload>(ObjectClass, Payload);
    if (RawResult.IsFailure()) {
        return TGameStatusResult<TPooledObjectHandle<T>>::Failure(RawResult.GetError().ErrorCode, RawResult.GetError().Message);
    }
    T* Object = RawResult.GetValue();
    const uint32 Gen = GetObjectGeneration(Object);
    return TGameStatusResult<TPooledObjectHandle<T>>::Success(TPooledObjectHandle<T>(Object, this, Gen));
}

template <typename T>
TGameStatusResult<TScopedPoolRental<T>> UObjectPoolSubsystem::AcquireScopedObject(
    TSubclassOf<UObject> ObjectClass
) {
    const TGameStatusResult<TPooledObjectHandle<T>> HandleResult = AcquireObjectHandle<T>(ObjectClass);
    if (HandleResult.IsFailure()) {
        return TGameStatusResult<TScopedPoolRental<T>>::Failure(HandleResult.GetError().ErrorCode, HandleResult.GetError().Message);
    }
    return TGameStatusResult<TScopedPoolRental<T>>::Success(TScopedPoolRental<T>(HandleResult.GetValue()));
}
