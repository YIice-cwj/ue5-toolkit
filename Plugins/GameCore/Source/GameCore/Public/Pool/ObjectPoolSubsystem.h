// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Pool/IGamePoolable.h"
#include "Pool/ObjectPoolTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tasks/FTimeSlicedTaskHandle.h"
#include "Templates/SubclassOf.h"

#include "ObjectPoolSubsystem.generated.h"

/**
 * @brief 世界级通用对象池子系统（支持 Actor 与纯 UObject 复用、分帧预热、按位组件重置与防 GC 顿卡）
 */
UCLASS()
class GAMECORE_API UObjectPoolSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
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
     * @brief 世界销毁生命周期回调：清空并销毁全部缓存的空闲 Actor 与 UObject 实例
     */
    void Deinitialize() override;

    /**
     * @brief 同步预热指定类别的 Actor 对象池
     * @param ActorClass 目标 Actor 类
     * @param PrewarmCount 预热生成的空闲实例数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount);

    /**
     * @brief 分帧渐进预热指定类别的 Actor 对象池（委托 UTimeSlicedTaskSubsystem 受帧预算约束调度）
     * @param ActorClass 目标 Actor 类
     * @param PrewarmCount 预热生成的空闲实例总数量
     * @param MaxSpawnsPerStep 单步最大生成实例数（默认每步 1 个）
     * @return 时间切片任务控制句柄，可用于查询进度或中途取消
     */
    FTimeSlicedTaskHandle PrewarmTimeSliced(
        TSubclassOf<AActor> ActorClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1);

    /**
     * @brief 设置指定 Actor 类别的最大空闲实例缓存容量（防长期闲置内存膨胀）
     * @param ActorClass 目标 Actor 类
     * @param InMaxIdleCount 最大空闲数（<=0 表示无限制）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
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
     * @brief 从池中借出或生成 Actor 实例
     * @param ActorClass 目标类
     * @param SpawnTransform 初始空间变换
     * @return 激活的 Actor 实例指针；类无效、生成失败或生命周期回调已归还/销毁实例时返回 nullptr
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool", meta = (DeterminesOutputType = "ActorClass"))
    AActor* AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform);

    /**
     * @brief 从池中借出或生成 Actor 实例（泛型强转）
     * @tparam T 返回的 Actor 具体派生类型
     * @param ActorClass 目标类
     * @param SpawnTransform 初始空间变换
     * @return 激活的 Actor 实例指针；类无效、生成失败、生命周期回调已归还/销毁实例或泛型强转失败返回 nullptr
     */
    template <typename T = AActor>
    [[nodiscard]] T* AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform = FTransform::Identity);

    /**
     * @brief 从池中借出或生成 Actor 实例并注入初始业务载荷
     * @tparam T 返回的 Actor 具体派生类型
     * @tparam TPayload 业务载荷结构体类型
     * @param ActorClass 目标类
     * @param SpawnTransform 初始空间变换
     * @param Payload 业务初始化载荷
     * @return 激活的 Actor 实例指针
     */
    template <typename T = AActor, typename TPayload>
    [[nodiscard]] T* AcquireActorWithPayload(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        const TPayload& Payload
    );

    /**
     * @brief 将已借出的 Actor 归还回池休眠（非活跃池属实例与重复归还直接忽略）
     * @param Actor 待归还的实例
     *
     * 实现思路：先从活跃集合摘除并解绑 OnDestroyed 委托，再依次执行生命周期回调、解除附加、清理顿帧、
     * 按该类 ResetFlags 策略选择性重置定时器/物理/移动/音频与 FXSystem（含 Niagara 与 Cascade），
     * 最后按最新容量策略休眠入池或销毁。
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ReleaseActor(AActor* Actor);

    /**
     * @brief 查询指定 Actor 类别的池状态
     * @param ActorClass 目标类
     * @param OutTotalCount 总实例数
     * @param OutActiveCount 当前借出活跃数
     * @param OutIdleCount 当前池内空闲数
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Pool")
    void GetPoolStats(TSubclassOf<AActor> ActorClass, int32& OutTotalCount, int32& OutActiveCount, int32& OutIdleCount) const;

    /**
     * @brief 清空并销毁指定 Actor 类别的所有空闲实例
     * @param ActorClass 目标类
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ClearPool(TSubclassOf<AActor> ActorClass);

    /**
     * @brief 清空并销毁所有类别的空闲实例（同时清理 Actor 池与非 Actor UObject 池）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ClearAllPools();

    /**
     * @brief 同步预热指定非 Actor UObject 类别的对象池
     * @param ObjectClass 目标 UObject 派生类（不可为抽象类或 AActor 派生类）
     * @param PrewarmCount 预热生成的空闲实例数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void PrewarmObjectPool(TSubclassOf<UObject> ObjectClass, int32 PrewarmCount);

    /**
     * @brief 设置指定非 Actor UObject 类别的最大空闲缓存容量
     * @param ObjectClass 目标 UObject 派生类
     * @param InMaxIdleCount 最大空闲数（<=0 表示无限制）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void SetMaxObjectPoolCapacity(TSubclassOf<UObject> ObjectClass, int32 InMaxIdleCount);

    /**
     * @brief 从池中借出或构建非 Actor UObject 实例
     * @param ObjectClass 目标 UObject 派生类（不可为抽象类或 AActor 派生类）
     * @return 激活的 UObject 实例指针；类无效或回调内自归还/销毁时返回 nullptr
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool", meta = (DeterminesOutputType = "ObjectClass"))
    UObject* AcquireObject(TSubclassOf<UObject> ObjectClass);

    /**
     * @brief 从池中借出或构建非 Actor UObject 实例（泛型强转）
     * @tparam T 返回的 UObject 具体派生类型
     * @param ObjectClass 目标类（默认使用 T::StaticClass()）
     * @return 激活的具体派生实例指针；失败返回 nullptr
     */
    template <typename T>
    [[nodiscard]] T* AcquireObject(TSubclassOf<UObject> ObjectClass = T::StaticClass());

    /**
     * @brief 从池中借出或构建非 Actor UObject 实例并注入初始业务载荷
     * @tparam T 返回的 UObject 具体派生类型
     * @tparam TPayload 业务载荷结构体类型
     * @param ObjectClass 目标类
     * @param Payload 业务初始化载荷
     * @return 激活的具体派生实例指针
     */
    template <typename T = UObject, typename TPayload>
    [[nodiscard]] T* AcquireObjectWithPayload(
        TSubclassOf<UObject> ObjectClass,
        const TPayload& Payload
    );

    /**
     * @brief 将已借出的非 Actor UObject 归还回池缓存（非活跃池属实例或重复归还直接忽略）
     * @param Object 待归还的 UObject 实例
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ReleaseObject(UObject* Object);

    /**
     * @brief 查询指定非 Actor UObject 类别的池容量与活跃统计快照
     * @param ObjectClass 目标 UObject 派生类
     * @return 池状态统计结构体
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Pool")
    [[nodiscard]] FPoolStats GetObjectPoolStats(TSubclassOf<UObject> ObjectClass) const;

    /**
     * @brief 清空并回收指定非 Actor UObject 类别的所有空闲实例
     * @param ObjectClass 目标 UObject 派生类
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ClearObjectPool(TSubclassOf<UObject> ObjectClass);

    /**
     * @brief 清空并回收所有非 Actor UObject 类别的空闲实例
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ClearAllObjectPools();

private:
    UFUNCTION()
    void OnPooledActorDestroyed(AActor* DestroyedActor);

    bool SpawnIdleActorToPool(TSubclassOf<AActor> ActorClass);

    void DestroyBucketIdleActors(FPoolBucket& Bucket);

    void DestroyBucketIdleObjects(FObjectPoolBucket& Bucket);

private:
    UPROPERTY()
    TMap<TSubclassOf<AActor>, FPoolBucket> PoolMap;

    UPROPERTY()
    TMap<TObjectPtr<UClass>, FObjectPoolBucket> ObjectPoolMap;
};

template <typename T>
T* UObjectPoolSubsystem::AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform) {
    return Cast<T>(AcquireActor(ActorClass, SpawnTransform));
}

template <typename T, typename TPayload>
T* UObjectPoolSubsystem::AcquireActorWithPayload(
    TSubclassOf<AActor> ActorClass,
    const FTransform& SpawnTransform,
    const TPayload& Payload
) {
    AActor* RawActor = AcquireActor(ActorClass, SpawnTransform);
    if (RawActor != nullptr && RawActor->GetClass()->ImplementsInterface(UGamePoolable::StaticClass())) {
        if (IGamePoolable* Poolable = Cast<IGamePoolable>(RawActor)) {
            Poolable->OnAcquireFromPoolWithPayload(&Payload);
        }
    }
    return Cast<T>(RawActor);
}

template <typename T>
T* UObjectPoolSubsystem::AcquireObject(TSubclassOf<UObject> ObjectClass) {
    return Cast<T>(AcquireObject(ObjectClass));
}

template <typename T, typename TPayload>
T* UObjectPoolSubsystem::AcquireObjectWithPayload(
    TSubclassOf<UObject> ObjectClass,
    const TPayload& Payload
) {
    UObject* RawObject = AcquireObject(ObjectClass);
    if (RawObject != nullptr && RawObject->GetClass()->ImplementsInterface(UGamePoolable::StaticClass())) {
        if (IGamePoolable* Poolable = Cast<IGamePoolable>(RawObject)) {
            Poolable->OnAcquireFromPoolWithPayload(&Payload);
        }
    }
    return Cast<T>(RawObject);
}
