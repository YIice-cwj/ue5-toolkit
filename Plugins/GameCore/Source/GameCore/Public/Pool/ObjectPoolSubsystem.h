// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/SubclassOf.h"
#include "GameFramework/Actor.h"

#include "ObjectPoolSubsystem.generated.h"

USTRUCT()
struct FPoolBucket {
    GENERATED_BODY()

    UPROPERTY()
    TArray<TObjectPtr<AActor>> IdleActors;

    UPROPERTY()
    TSet<TObjectPtr<AActor>> IdleActorSet;

    int32 ActiveCount = 0;

    /** 单类对象池最大空闲缓存数（<=0 表示不限制容量，默认 100） */
    int32 MaxIdleCount = 100;
};

/**
 * @brief 世界级通用对象池子系统（Actor 自动化复用、休眠、唤醒与防 GC 顿卡）
 */
UCLASS()
class GAMECORE_API UObjectPoolSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
    static UObjectPoolSubsystem& Get(const UObject* WorldContextObject);
    static UObjectPoolSubsystem* GetPtr(const UObject* WorldContextObject);

    void Deinitialize() override;

    /**
     * @brief 预热指定类别的对象池
     * @param ActorClass 目标 Actor 类
     * @param PrewarmCount 预热生成的空闲实例数量
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount);

    /**
     * @brief 设置指定类别的最大空闲实例缓存容量（防长期闲置内存泄漏）
     * @param ActorClass 目标 Actor 类
     * @param InMaxIdleCount 最大空闲数（<=0 表示无限制）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, int32 InMaxIdleCount);

    /**
     * @brief 从池中借出或生成 Actor 实例
     * @param ActorClass 目标类
     * @param SpawnTransform 初始空间变换
     * @return 激活的 Actor 实例指针
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool", meta = (DeterminesOutputType = "ActorClass"))
    AActor* AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform);

    /**
     * @brief 将已借出的 Actor 归还回池休眠
     * @param Actor 待归还的实例
     *
     * 实现思路：先解绑 OnDestroyed 委托（避免正常归还触发外部销毁扣减），
     * 再依次执行生命周期回调、解除附加、清理定时器、重置物理与移动、停止声音与粒子，最后休眠入池。
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ReleaseActor(AActor* Actor);

    /**
     * @brief 查询指定类别的池状态
     * @param ActorClass 目标类
     * @param OutTotalCount 总实例数
     * @param OutActiveCount 当前借出活跃数
     * @param OutIdleCount 当前池内空闲数
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Pool")
    void GetPoolStats(TSubclassOf<AActor> ActorClass, int32& OutTotalCount, int32& OutActiveCount, int32& OutIdleCount) const;

    /**
     * @brief 清空并销毁指定类别的所有空闲实例
     * @param ActorClass 目标类
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ClearPool(TSubclassOf<AActor> ActorClass);

    /**
     * @brief 清空并销毁所有类别的空闲实例
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Pool")
    void ClearAllPools();

    /**
     * @brief 从池中借出或生成 Actor 实例（泛型强转）
     * @tparam T 返回的 Actor 具体派生类型
     * @param ActorClass 目标类
     * @param SpawnTransform 初始空间变换
     * @return 激活的 Actor 实例指针
     */
    template <typename T = AActor>
    [[nodiscard]] T* AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform = FTransform::Identity);

private:
    UFUNCTION()
    void OnPooledActorDestroyed(AActor* DestroyedActor);

    void DestroyBucketIdleActors(FPoolBucket& Bucket);

private:
    UPROPERTY()
    TMap<TSubclassOf<AActor>, FPoolBucket> PoolMap;
};

template <typename T>
T* UObjectPoolSubsystem::AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform) {
    return Cast<T>(AcquireActor(ActorClass, SpawnTransform));
}

