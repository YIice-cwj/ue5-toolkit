// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ObjectPool/ObjectPoolTypes.h"
#include "ObjectPool/PoolGenerationTracker.h"
#include "Types/TGameResult.h"

class UObjectPoolSubsystem;

/**
 * @brief Actor 对象池底层核心管理器（负责 Actor 实例的生成、缓存桶管理、休眠激活与状态消杀）
 */
class FActorPoolManager {
public:
    /**
     * @brief 初始化 Actor 池管理器
     * @param InWorld 所属 World 上下文
     * @param InGenTracker 世代追踪器指针
     * @param InSubsystemOwner 所属子系统对象指针（用于绑定委托）
     */
    void Initialize(UWorld* InWorld, FPoolGenerationTracker* InGenTracker, UObjectPoolSubsystem* InSubsystemOwner);

    /**
     * @brief 同步预热指定类别的 Actor
     * @param ActorClass 目标 Actor 类
     * @param PrewarmCount 预热生成的实例数量
     */
    void Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount);

    /**
     * @brief 生成单个空闲 Actor 实例并放入空闲池
     * @param ActorClass 目标 Actor 类
     * @return 成功生成并入池返回 true
     */
    bool SpawnIdleActorToPool(TSubclassOf<AActor> ActorClass);

    /**
     * @brief 设置指定 Actor 类的最大空闲缓存容量
     * @param ActorClass 目标 Actor 类
     * @param InMaxIdleCount 最大空闲数（<=0 表示无限制）
     */
    void SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, int32 InMaxIdleCount);

    /**
     * @brief 配置指定 Actor 类别的组件自动重置策略掩码
     * @param ActorClass 目标 Actor 类
     * @param InResetFlags 重置位掩码
     */
    void SetPoolResetFlags(TSubclassOf<AActor> ActorClass, EPoolResetFlags InResetFlags);

    /**
     * @brief 查询指定 Actor 类别的组件自动重置策略掩码
     * @param ActorClass 目标 Actor 类
     * @return 当前配置的组件重置策略掩码
     */
    [[nodiscard]] EPoolResetFlags GetPoolResetFlags(TSubclassOf<AActor> ActorClass) const;

    /**
     * @brief 查询指定 Actor 类别的池容量与活跃统计快照
     * @param ActorClass 目标 Actor 类
     * @return 池状态快照结构体
     */
    [[nodiscard]] FPoolStats GetPoolStats(TSubclassOf<AActor> ActorClass) const;

    /**
     * @brief 从池中借出或生成 Actor 实例
     * @param ActorClass 目标 Actor 类
     * @param SpawnTransform 初始空间变换
     * @return 成功返回 Actor 原生指针，失败返回 Failure
     */
    TGameStatusResult<AActor*> AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform);

    /**
     * @brief 将已借出的 Actor 归还回池休眠
     * @param Actor 待归还的实例
     * @return 成功归还返回 Success，未借出或已销毁返回 Failure
     */
    TGameStatusResult<void> ReleaseActor(AActor* Actor);

    /**
     * @brief 清空并销毁指定 Actor 类别的所有空闲实例
     * @param ActorClass 目标 Actor 类
     */
    void ClearPool(TSubclassOf<AActor> ActorClass);

    /**
     * @brief 清空并销毁所有类别的空闲 Actor 实例
     */
    void ClearAllPools();

    /**
     * @brief 响应池化 Actor 外部意外销毁的回调
     * @param DestroyedActor 被销毁的 Actor 实例
     */
    void OnPooledActorDestroyed(AActor* DestroyedActor);

    /**
     * @brief 销毁指定存储桶中的所有空闲 Actor 实例
     * @param Bucket 目标 Actor 存储桶
     */
    void DestroyBucketIdleActors(FPoolBucket& Bucket);

    /**
     * @brief 查找指定 Actor 类的存储桶指针
     * @param ActorClass 目标 Actor 类
     * @return 存储桶指针，不存在返回 nullptr
     */
    [[nodiscard]] const FPoolBucket* FindBucket(TSubclassOf<AActor> ActorClass) const {
        return PoolMap.Find(ActorClass);
    }

    /**
     * @brief 查询或添加指定 Actor 类的存储桶引用
     * @param ActorClass 目标 Actor 类
     * @return 存储桶引用
     */
    FPoolBucket& FindOrAddBucket(TSubclassOf<AActor> ActorClass) {
        return PoolMap.FindOrAdd(ActorClass);
    }

    /**
     * @brief 获取当前注册的 Actor 池类别总数
     * @return 池总数
     */
    [[nodiscard]] int32 GetPoolCount() const noexcept {
        return PoolMap.Num();
    }

private:
    static void DeactivateActorForPool(AActor* Actor);

    UWorld* World = nullptr;
    FPoolGenerationTracker* GenerationTracker = nullptr;
    UObjectPoolSubsystem* SubsystemOwner = nullptr;
    TMap<TSubclassOf<AActor>, FPoolBucket> PoolMap;
};
