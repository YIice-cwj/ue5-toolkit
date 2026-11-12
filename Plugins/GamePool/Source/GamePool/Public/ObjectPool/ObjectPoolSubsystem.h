// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ObjectPool/ActorPoolManager.h"
#include "ObjectPool/IGamePoolable.h"
#include "ObjectPool/ObjectPoolTypes.h"
#include "ObjectPool/PoolGenerationTracker.h"
#include "ObjectPool/PooledObjectHandle.h"
#include "ObjectPool/UObjectPoolManager.h"
#include "Types/TGameResult.h"
#include "Subsystems/WorldSubsystem.h"
#include "Async/FCancellationToken.h"
#include "Async/TGameCoroTask.h"
#include "Containers/TLockFreeRingBuffer.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPath.h"

#include "ObjectPoolSubsystem.generated.h"

/**
 * @brief 世界级通用对象池子系统（支持 Actor 与纯 UObject 复用、分帧预热、软引用协程预热、代数安全句柄与防 GC 顿卡）
 */
UCLASS()
class GAMEPOOL_API UObjectPoolSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
    UObjectPoolSubsystem();
    virtual ~UObjectPoolSubsystem() override;

    /**
     * @brief 获取对象池子系统引用（不存在时断言失败）
     * @param WorldContextObject 关联的世界上下文对象
     * @return 子系统引用
     */
    static UObjectPoolSubsystem& Get(const UObject* WorldContextObject);

    /**
     * @brief 安全获取对象池子系统指针（上下文无效返回 nullptr）
     * @param WorldContextObject 关联的世界上下文对象
     * @return 子系统指针，未就绪时返回 nullptr
     */
    static UObjectPoolSubsystem* GetPtr(const UObject* WorldContextObject);

    /**
     * @brief 世界子系统初始化生命周期
     * @param Collection 子系统依赖集合
     */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 世界销毁生命周期回调：清空并销毁全部缓存的空闲 Actor 与 UObject 实例
     */
    virtual void Deinitialize() override;

    /**
     * @brief 同步预热指定类别的 Actor 对象池
     * @param ActorClass 目标 Actor 类
     * @param PrewarmCount 预热生成的空闲实例数量
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount);

    /**
     * @brief C++20 协程分帧渐进预热 Actor 对象池（按帧预算平滑切片，支持协作式取消与生命周期自毁熔断）
     * @param ActorClass 目标 Actor 类
     * @param PrewarmCount 预热生成的空闲实例总数量
     * @param MaxSpawnsPerStep 单步最大生成实例数（默认每步 1 个）
     * @param CancellationToken 协作式取消令牌（可选）
     * @return 实际成功预热生成的实例总数异步协程任务
     */
    TGameCoroTask<int32> PrewarmAsync(
        TSubclassOf<AActor> ActorClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 基于软引用的 C++20 协程流式异步预热（支持软引用异步流式加载、帧预算切片与取消熔断）
     * @param ActorClassPath 目标 Actor 类的软对象路径
     * @param PrewarmCount 预热生成的空闲实例总数量
     * @param MaxSpawnsPerStep 单步最大生成实例数（默认每步 1 个）
     * @param CancellationToken 协作式取消令牌（可选）
     * @return 实际成功预热生成的实例总数异步协程任务
     */
    TGameCoroTask<int32> PrewarmAsync(
        const FSoftObjectPath& ActorClassPath,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 设置指定 Actor 类别的最大空闲实例缓存容量（防长期闲置内存膨胀）
     * @param ActorClass 目标 Actor 类
     * @param InMaxIdleCount 最大空闲数（<=0 表示无限制）
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, int32 InMaxIdleCount);

    /**
     * @brief 配置指定 Actor 类别归还入池时的组件自动重置策略掩码
     * @param ActorClass 目标 Actor 类
     * @param InResetFlags 组件重置位掩码（默认 EPoolResetFlags::All）
     */
    void SetPoolResetFlags(TSubclassOf<AActor> ActorClass, EPoolResetFlags InResetFlags);

    /**
     * @brief 查询指定 Actor 类别当前的组件自动重置策略掩码
     * @param ActorClass 目标 Actor 类
     * @return 当前配置的组件重置位掩码（未显式配置时返回 EPoolResetFlags::All）
     */
    [[nodiscard]] EPoolResetFlags GetPoolResetFlags(TSubclassOf<AActor> ActorClass) const;

    /**
     * @brief 从池中借出或生成 Actor 实例（蓝图轻量门面）
     * @param ActorClass 目标类
     * @param SpawnTransform 初始空间变换
     * @return 激活的 Actor 实例指针；失败返回 nullptr
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Acquire Actor", DeterminesOutputType = "ActorClass"))
    AActor* K2_AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform);

    /**
     * @brief 从池中借出或生成 Actor 实例（泛型强类型契约）
     * @tparam T 返回的 Actor 具体派生类型
     * @param ActorClass 目标类
     * @param SpawnTransform 初始空间变换
     * @return 成功返回派生类指针，失败返回 Failure(5001/5002)
     */
    template <typename T = AActor>
    [[nodiscard]] TGameStatusResult<T*> AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform = FTransform::Identity);

    /**
     * @brief 从池中借出或生成 Actor 实例并注入初始业务载荷
     * @tparam T 返回的 Actor 具体派生类型
     * @tparam TPayload 业务载荷结构体类型
     * @param ActorClass 目标类
     * @param SpawnTransform 初始空间变换
     * @param Payload 业务初始化载荷
     * @return 成功返回派生类指针，失败返回 Failure(5001/5002)
     */
    template <typename T = AActor, typename TPayload>
    [[nodiscard]] TGameStatusResult<T*> AcquireActorWithPayload(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        const TPayload& Payload
    );

    /**
     * @brief 从池中借出 Actor 并获取带世代防悬空校验的安全句柄
     * @tparam T 返回的 Actor 具体派生类型
     * @param ActorClass 目标类
     * @param SpawnTransform 初始空间变换
     * @return 成功返回安全受管句柄
     */
    template <typename T = AActor>
    [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireActorHandle(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform = FTransform::Identity
    );

    /**
     * @brief 从池中借出 Actor 注入载荷并获取带世代防悬空校验的安全句柄
     * @tparam T 返回的 Actor 具体派生类型
     * @tparam TPayload 业务载荷类型
     * @param ActorClass 目标类
     * @param SpawnTransform 初始空间变换
     * @param Payload 业务初始化载荷
     * @return 成功返回安全受管句柄
     */
    template <typename T = AActor, typename TPayload>
    [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireActorHandleWithPayload(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        const TPayload& Payload
    );

    /**
     * @brief 从池中借出 Actor 并包装为作用域 RAII 自动归还租约
     * @tparam T 返回的 Actor 具体派生类型
     * @param ActorClass 目标类
     * @param SpawnTransform 初始空间变换
     * @return 成功返回租约卫士
     */
    template <typename T = AActor>
    [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedActor(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform = FTransform::Identity
    );

    /**
     * @brief 将已借出的 Actor 归还回池休眠（蓝图轻量门面）
     * @param Actor 待归还的实例
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Release Actor"))
    void K2_ReleaseActor(AActor* Actor);

    /**
     * @brief 将已借出的 Actor 归还回池休眠（强类型契约）
     * @param Actor 待归还的实例
     * @return 成功归还返回 Success，未借出或已销毁返回 Failure(5003/5004)
     */
    TGameStatusResult<void> ReleaseActor(AActor* Actor);

    /**
     * @brief 通过安全受管句柄归还 Actor 回池
     * @tparam T 具体类型
     * @param Handle 安全句柄
     * @return 成功归还返回 Success，句柄失效返回 Failure
     */
    template <typename T>
    TGameStatusResult<void> ReleaseActor(const TPooledObjectHandle<T>& Handle) {
        if (!Handle.IsValid()) {
            return TGameStatusResult<void>::Failure(5003, TEXT("Handle is invalid or expired"));
        }
        return ReleaseActor(Handle.Get());
    }

    /**
     * @brief 在工作线程或异步任务中以无锁方式安全将 Actor 压入延迟归还队列
     * @param Actor 待归还的实例
     * @return 成功压入返回 true，队列已满返回 false
     */
    bool ReleaseActorDeferred(AActor* Actor);

    /**
     * @brief 主线程批量收割并完成延迟队列中的 Actor 归还
     * @param MaxProcessCount 单次最大处理实例数
     * @return 实际成功收割归还的实例数
     */
    int32 ProcessDeferredReleases(int32 MaxProcessCount = 64);

    /**
     * @brief 查询指定 Actor 类别的池容量与活跃统计快照
     * @param ActorClass 目标 Actor 类
     * @return 池状态统计结构体
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|ObjectPool")
    [[nodiscard]] FPoolStats GetPoolStats(TSubclassOf<AActor> ActorClass) const;

    /**
     * @brief 清空并销毁指定 Actor 类别的所有空闲实例
     * @param ActorClass 目标类
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearPool(TSubclassOf<AActor> ActorClass);

    /**
     * @brief 清空并销毁所有类别的空闲实例（同时清理 Actor 池与非 Actor UObject 池）
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearAllPools();

    /**
     * @brief 同步预热指定非 Actor UObject 类别的对象池
     * @param ObjectClass 目标 UObject 派生类（不可为抽象类或 AActor 派生类）
     * @param PrewarmCount 预热生成的空闲实例数量
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void PrewarmObjectPool(TSubclassOf<UObject> ObjectClass, int32 PrewarmCount);

    /**
     * @brief 设置指定非 Actor UObject 类别的最大空闲缓存容量
     * @param ObjectClass 目标 UObject 派生类
     * @param InMaxIdleCount 最大空闲数（<=0 表示无限制）
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void SetMaxObjectPoolCapacity(TSubclassOf<UObject> ObjectClass, int32 InMaxIdleCount);

    /**
     * @brief 从池中借出或构建非 Actor UObject 实例（蓝图轻量门面）
     * @param ObjectClass 目标 UObject 派生类（不可为抽象类或 AActor 派生类）
     * @return 激活的 UObject 实例指针；失败返回 nullptr
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Acquire Object", DeterminesOutputType = "ObjectClass"))
    UObject* K2_AcquireObject(TSubclassOf<UObject> ObjectClass);

    /**
     * @brief 从池中借出或构建非 Actor UObject 实例（泛型强类型契约）
     * @tparam T 返回的 UObject 具体派生类型
     * @param ObjectClass 目标类（默认使用 T::StaticClass()）
     * @return 成功返回派生类指针，失败返回 Failure(5001/5002)
     */
    template <typename T = UObject>
    [[nodiscard]] TGameStatusResult<T*> AcquireObject(TSubclassOf<UObject> ObjectClass = T::StaticClass());

    /**
     * @brief 从池中借出或构建非 Actor UObject 实例并注入初始业务载荷
     * @tparam T 返回的 UObject 具体派生类型
     * @tparam TPayload 业务载荷结构体类型
     * @param ObjectClass 目标类
     * @param Payload 业务初始化载荷
     * @return 成功返回派生类指针，失败返回 Failure(5001/5002)
     */
    template <typename T = UObject, typename TPayload>
    [[nodiscard]] TGameStatusResult<T*> AcquireObjectWithPayload(
        TSubclassOf<UObject> ObjectClass,
        const TPayload& Payload
    );

    /**
     * @brief 从池中借出 UObject 并获取带世代防悬空校验的安全句柄
     * @tparam T 返回的 UObject 具体类型
     * @param ObjectClass 目标类
     * @return 成功返回安全受管句柄
     */
    template <typename T = UObject>
    [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireObjectHandle(
        TSubclassOf<UObject> ObjectClass = T::StaticClass()
    );

    /**
     * @brief 从池中借出 UObject 注入载荷并获取带世代防悬空校验的安全句柄
     * @tparam T 返回的 UObject 具体类型
     * @tparam TPayload 业务载荷类型
     * @param ObjectClass 目标类
     * @param Payload 业务初始化载荷
     * @return 成功返回安全受管句柄
     */
    template <typename T = UObject, typename TPayload>
    [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireObjectHandleWithPayload(
        TSubclassOf<UObject> ObjectClass,
        const TPayload& Payload
    );

    /**
     * @brief 从池中借出 UObject 并包装为作用域 RAII 自动归还租约
     * @tparam T 返回的 UObject 具体类型
     * @param ObjectClass 目标类
     * @return 成功返回租约卫士
     */
    template <typename T = UObject>
    [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedObject(
        TSubclassOf<UObject> ObjectClass = T::StaticClass()
    );

    /**
     * @brief 将已借出的非 Actor UObject 归还回池缓存（蓝图轻量门面）
     * @param Object 待归还的 UObject 实例
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Release Object"))
    void K2_ReleaseObject(UObject* Object);

    /**
     * @brief 将已借出的非 Actor UObject 归还回池缓存（强类型契约）
     * @param Object 待归还的 UObject 实例
     * @return 成功归还返回 Success，未借出或已销毁返回 Failure(5003/5004)
     */
    TGameStatusResult<void> ReleaseObject(UObject* Object);

    /**
     * @brief 通过安全受管句柄归还 UObject 回池
     * @tparam T 具体类型
     * @param Handle 安全句柄
     * @return 成功归还返回 Success，句柄失效返回 Failure
     */
    template <typename T>
    TGameStatusResult<void> ReleaseObject(const TPooledObjectHandle<T>& Handle) {
        if (!Handle.IsValid()) {
            return TGameStatusResult<void>::Failure(5003, TEXT("Handle is invalid or expired"));
        }
        return ReleaseObject(Handle.Get());
    }

    /**
     * @brief 查询指定非 Actor UObject 类别的池容量与活跃统计快照
     * @param ObjectClass 目标 UObject 派生类
     * @return 池状态统计结构体
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|ObjectPool")
    [[nodiscard]] FPoolStats GetObjectPoolStats(TSubclassOf<UObject> ObjectClass) const;

    /**
     * @brief 清空并回收指定非 Actor UObject 类别的所有空闲实例
     * @param ObjectClass 目标 UObject 派生类
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearObjectPool(TSubclassOf<UObject> ObjectClass);

    /**
     * @brief 清空并回收所有非 Actor UObject 类别的空闲实例
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearAllObjectPools();

    /**
     * @brief 校验 Actor 实例句柄记录的世代是否与池内活跃代数一致
     * @param Actor 目标 Actor 实例
     * @param InGeneration 待校验世代
     * @return 活跃且世代一致返回 true
     */
    [[nodiscard]] bool ValidateHandleGeneration(const AActor* Actor, uint32 InGeneration) const;

    /**
     * @brief 校验 UObject 实例句柄记录的世代是否与池内活跃代数一致
     * @param Object 目标 UObject 实例
     * @param InGeneration 待校验世代
     * @return 活跃且世代一致返回 true
     */
    [[nodiscard]] bool ValidateHandleGeneration(const UObject* Object, uint32 InGeneration) const;

    /**
     * @brief 获取 Actor 实例当前的有效借出世代代数
     * @param Actor 目标 Actor 实例
     * @return 世代代数
     */
    [[nodiscard]] uint32 GetActorGeneration(const AActor* Actor) const;

    /**
     * @brief 获取 UObject 实例当前的有效借出世代代数
     * @param Object 目标 UObject 实例
     * @return 世代代数
     */
    [[nodiscard]] uint32 GetObjectGeneration(const UObject* Object) const;

    /**
     * @brief 内部借出或生成 Actor 实例核心实现
     * @param ActorClass 目标类
     * @param SpawnTransform 初始空间变换
     * @return 成功返回 Actor 指针，失败返回 Failure
     */
    TGameStatusResult<AActor*> AcquireActorInternal(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform);

    /**
     * @brief 内部借出或构建非 Actor UObject 实例核心实现
     * @param ObjectClass 目标 UObject 派生类
     * @return 成功返回 UObject 指针，失败返回 Failure
     */
    TGameStatusResult<UObject*> AcquireObjectInternal(TSubclassOf<UObject> ObjectClass);

    /**
     * @brief 生成单个空闲 Actor 实例并放入空闲池
     * @param ActorClass 目标 Actor 类
     * @return 成功生成并入池返回 true
     */
    bool SpawnIdleActorToPool(TSubclassOf<AActor> ActorClass);

    /**
     * @brief 响应池化 Actor 外部意外销毁的回调
     * @param DestroyedActor 被销毁的 Actor 实例
     */
    UFUNCTION()
    void OnPooledActorDestroyed(AActor* DestroyedActor);

private:
    FPoolGenerationTracker GenerationTracker;
    FActorPoolManager ActorManager;
    FUObjectPoolManager ObjectManager;
    TLockFreeRingBuffer<TWeakObjectPtr<AActor>, 1024> DeferredReleaseBuffer;
};

template <typename T>
bool TPooledObjectHandle<T>::IsValid() const {
    if (!ObjectPtr.IsValid() || !SubsystemPtr.IsValid()) {
        return false;
    }
    return SubsystemPtr->ValidateHandleGeneration(ObjectPtr.Get(), Generation);
}

template <typename T>
void TScopedPoolRental<T>::EarlyRelease() {
    if (Handle.IsValid()) {
        if (UObjectPoolSubsystem* Subsystem = Handle.GetSubsystem()) {
            if constexpr (std::is_base_of_v<AActor, T>) {
                Subsystem->ReleaseActor(Handle.Get());
            } else {
                Subsystem->ReleaseObject(Handle.Get());
            }
        }
        Handle.Reset();
    }
}

template <typename T>
TGameStatusResult<T*> UObjectPoolSubsystem::AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform) {
    const TGameStatusResult<AActor*> RawResult = AcquireActorInternal(ActorClass, SpawnTransform);
    if (RawResult.IsFailure()) {
        return TGameStatusResult<T*>::Failure(RawResult.GetError().ErrorCode, RawResult.GetError().Message);
    }
    T* Casted = Cast<T>(RawResult.GetValue());
    if (Casted == nullptr) {
        return TGameStatusResult<T*>::Failure(5001, TEXT("Cast to requested Actor class failed"));
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
        return TGameStatusResult<T*>::Failure(5001, TEXT("Cast to requested UObject class failed"));
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
