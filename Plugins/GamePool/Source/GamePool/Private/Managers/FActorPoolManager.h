/**
 * @file FActorPoolManager.h
 * @brief Actor 对象池底层核心内部管理器（负责实例生成、TDeque 存储桶管理、GC 引用追踪、休眠激活与状态清洗）
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/FPoolAcquirePayload.h"
#include "Core/ObjectPoolTypes.h"
#include "GameFramework/Actor.h"
#include "Handle/FPoolGenerationTracker.h"
#include "Managers/TPoolBucket.h"
#include "Types/TGameResult.h"

class UObjectPoolSubsystem;

/**
 * @brief Actor 对象池底层核心管理器（仅由 UObjectPoolSubsystem 内部持有编排）
 */
class FActorPoolManager {
public:
    /**
     * @brief 初始化 Actor 池管理器上下文
     * @param InWorld 所属 World 上下文
     * @param InGenTracker 世代追踪器指针
     * @param InSubsystemOwner 所属子系统对象指针（用于绑定外部销毁自愈委托）
     */
    void Initialize(UWorld* InWorld, FPoolGenerationTracker* InGenTracker, UObjectPoolSubsystem* InSubsystemOwner);

    /**
     * @brief 反初始化管理器，清空全部空闲实例、平衡全部 Stat 计数并重置存储桶
     */
    void Deinitialize();

    /**
     * @brief 同步预热指定类别的 Actor 到空闲池（受 MaxIdleCount 上限约束）
     * @param ActorClass 目标 Actor 类
     * @param PrewarmCount 预热生成的实例数量
     */
    void Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount);

    /**
     * @brief 生成单个空闲 Actor 实例并放入空闲池（受 MaxIdleCount 上限约束）
     * @param ActorClass 目标 Actor 类
     * @return 成功生成并入池返回 Success，失败返回带 Game::ModuleId::Pool 的 Failure
     */
    TGameStatusResult<void> SpawnIdleActorToPool(TSubclassOf<AActor> ActorClass);

    /**
     * @brief 设置指定 Actor 类的最大空闲缓存容量（超出部分按冷端优先立即裁剪销毁）
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
     * @brief 配置指定 Actor 类别的闲置弹性缩容与淘汰策略
     * @param ActorClass 目标 Actor 类
     * @param InSettings 淘汰策略配置
     */
    void SetPoolEvictionSettings(TSubclassOf<AActor> ActorClass, const FPoolEvictionSettings& InSettings);

    /**
     * @brief 查询指定 Actor 类别的闲置弹性缩容与淘汰策略
     * @param ActorClass 目标 Actor 类
     * @return 当前配置的淘汰策略
     */
    [[nodiscard]] FPoolEvictionSettings GetPoolEvictionSettings(TSubclassOf<AActor> ActorClass) const;

    /**
     * @brief 计算所有启用淘汰的 Actor 池中的最小心跳检测周期
     * @param OutMinIntervalSeconds 传出最小心跳周期（秒）
     * @return 若至少有一个池启用淘汰返回 true，否则返回 false
     */
    [[nodiscard]] bool GetMinimumEvictionCheckIntervalSeconds(float& OutMinIntervalSeconds) const noexcept;

    /**
     * @brief 执行超时冷 Actor 实例平滑淘汰（O(1) FIFO 从最冷端逐出）
     * @param NowSeconds 当前单调硬件时间戳（秒）
     * @param MaxGlobalQuota 本次全局最大销毁总配额
     * @param bRespectPerBucketLimit 是否同时受各池自身 MaxEvictionsPerStep 上限约束
     * @return 实际销毁的冷实例总数
     */
    int32 EvictExpiredActors(double NowSeconds, int32 MaxGlobalQuota, bool bRespectPerBucketLimit);

    /**
     * @brief 查询指定 Actor 类别的池容量与活跃统计快照
     * @param ActorClass 目标 Actor 类
     * @return 池状态快照结构体
     */
    [[nodiscard]] FPoolStats GetPoolStats(TSubclassOf<AActor> ActorClass) const;

    /**
     * @brief 从池中借出或生成 Actor 实例（支持可选类型指纹载荷，并在全部回调后校验重入有效性）
     * @param ActorClass 目标 Actor 类
     * @param SpawnTransform 初始空间变换
     * @param Payload 可选业务载荷视图
     * @return 成功返回 Actor 原生指针，失败返回带 Game::ModuleId::Pool 的 Failure
     */
    TGameStatusResult<AActor*> AcquireActor(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        FPoolAcquirePayload Payload = FPoolAcquirePayload()
    );

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
     * @brief 响应池化 Actor 外部意外销毁的自愈回调（优先 O(1) 活跃集合移除并同步扣减 Stat）
     * @param DestroyedActor 被销毁的 Actor 实例
     */
    void OnPooledActorDestroyed(AActor* DestroyedActor);

    /**
     * @brief 泄洪销毁所有类别的空闲 Actor 实例（从最冷端逐出，优先保留最热保底实例）
     * @param bPreserveMinCount 是否保留各池配置的 MinPreservedCount 保底数量
     * @return 实际销毁的实例数
     */
    int32 FlushIdleActors(bool bPreserveMinCount = false);

    /**
     * @brief 查询指定 Actor 类别空闲池是否已达 MaxIdleCount 容量上限
     * @param ActorClass 目标 Actor 类
     * @return 已达上限返回 true
     */
    [[nodiscard]] bool IsAtIdleCapacity(TSubclassOf<AActor> ActorClass) const;

    /**
     * @brief 向 UE 垃圾回收系统注册全部 Class Key、空闲与活跃 Actor 强引用
     * @param Collector 引用收集器
     * @param ReferencingObject 引用宿主对象
     */
    void AddReferencedObjects(FReferenceCollector& Collector, const UObject* ReferencingObject = nullptr);

    /**
     * @brief 获取累计成功借出总次数
     * @return 借出总数
     */
    [[nodiscard]] int64 GetTotalAcquires() const noexcept {
        return TotalAcquires;
    }

    /**
     * @brief 获取累计借出复用命中次数
     * @return 命中总数
     */
    [[nodiscard]] int64 GetCacheHits() const noexcept {
        return CacheHits;
    }

    /**
     * @brief 获取累计穿透新生成次数
     * @return 穿透总数
     */
    [[nodiscard]] int64 GetCacheMisses() const noexcept {
        return CacheMisses;
    }

    /**
     * @brief 获取全部类别的活跃 Actor 实例总数
     * @return 活跃 Actor 实例总数
     */
    [[nodiscard]] int32 GetTotalActiveCount() const noexcept;

    /**
     * @brief 获取全部类别的空闲 Actor 实例总数
     * @return 空闲 Actor 实例总数
     */
    [[nodiscard]] int32 GetTotalIdleCount() const noexcept;

    /**
     * @brief 重置遥测统计计数器
     */
    void ResetTelemetry() noexcept;

private:
    void DestroyIdleActor(AActor* Actor);
    void DestroyBucketIdleActors(TPoolBucket<AActor>& Bucket);
    static void DeactivateActorForPool(AActor* Actor);

private:
    UWorld* World = nullptr;
    FPoolGenerationTracker* GenerationTracker = nullptr;
    UObjectPoolSubsystem* SubsystemOwner = nullptr;
    TMap<TObjectPtr<UClass>, TPoolBucket<AActor>> PoolMap;

    int64 TotalAcquires = 0;
    int64 CacheHits = 0;
    int64 CacheMisses = 0;
};
