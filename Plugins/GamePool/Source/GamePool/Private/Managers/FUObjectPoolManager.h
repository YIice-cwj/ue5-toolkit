/**
 * @file FUObjectPoolManager.h
 * @brief 纯 UObject 对象池底层核心内部管理器（负责非 Actor 纯 UObject 的生成、GC 引用追踪与休眠复用）
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/FPoolAcquirePayload.h"
#include "Core/ObjectPoolTypes.h"
#include "Handle/FPoolGenerationTracker.h"
#include "Managers/TPoolBucket.h"
#include "Types/TGameResult.h"
#include "UObject/Object.h"

/**
 * @brief 纯 UObject 对象池底层核心管理器（仅由 UObjectPoolSubsystem 内部持有，GC 追踪由子系统统一派发）
 */
class FUObjectPoolManager {
public:
    /**
     * @brief 初始化 UObject 池管理器
     * @param InOuter 所属 Outer 对象（通常为 UObjectPoolSubsystem）
     * @param InGenTracker 世代代数追踪器指针
     */
    void Initialize(UObject* InOuter, FPoolGenerationTracker* InGenTracker);

    /**
     * @brief 反初始化管理器，清理全部空闲实例、平衡全部 Stat 计数并清空存储桶
     */
    void Deinitialize();

    /**
     * @brief 同步预热指定纯 UObject 类别（受 MaxIdleCount 上限约束）
     * @param ObjectClass 目标 UObject 类
     * @param PrewarmCount 预热数量
     */
    void Prewarm(TSubclassOf<UObject> ObjectClass, int32 PrewarmCount);

    /**
     * @brief 生成单个空闲 UObject 实例并放入空闲池（受 MaxIdleCount 上限约束）
     * @param ObjectClass 目标类
     * @return 成功生成并入池返回 Success，失败返回带 Game::ModuleId::Pool 的 Failure
     */
    TGameStatusResult<void> SpawnIdleObjectToPool(TSubclassOf<UObject> ObjectClass);

    /**
     * @brief 设置指定 UObject 类的最大空闲缓存容量（超出部分按冷端优先立即裁剪回收）
     * @param ObjectClass 目标类
     * @param InMaxIdleCount 最大空闲数（<=0 表示无限制）
     */
    void SetMaxPoolCapacity(TSubclassOf<UObject> ObjectClass, int32 InMaxIdleCount);

    /**
     * @brief 配置指定纯 UObject 类别的闲置弹性缩容与淘汰策略
     * @param ObjectClass 目标类
     * @param InSettings 淘汰策略配置
     */
    void SetPoolEvictionSettings(TSubclassOf<UObject> ObjectClass, const FPoolEvictionSettings& InSettings);

    /**
     * @brief 查询指定纯 UObject 类别的闲置弹性缩容与淘汰策略
     * @param ObjectClass 目标类
     * @return 当前配置的淘汰策略
     */
    [[nodiscard]] FPoolEvictionSettings GetPoolEvictionSettings(TSubclassOf<UObject> ObjectClass) const;

    /**
     * @brief 计算所有启用淘汰的 UObject 池中的最小心跳检测周期
     * @param OutMinIntervalSeconds 传出最小心跳周期（秒）
     * @return 若至少有一个池启用淘汰返回 true，否则返回 false
     */
    [[nodiscard]] bool GetMinimumEvictionCheckIntervalSeconds(float& OutMinIntervalSeconds) const noexcept;

    /**
     * @brief 执行超时冷 UObject 实例平滑淘汰（O(1) FIFO 从最冷端逐出）
     * @param NowSeconds 当前单调硬件时间戳（秒）
     * @param MaxGlobalQuota 本次全局最大销毁总配额
     * @param bRespectPerBucketLimit 是否同时受各池自身 MaxEvictionsPerStep 上限约束
     * @return 实际销毁的冷实例总数
     */
    int32 EvictExpiredObjects(double NowSeconds, int32 MaxGlobalQuota, bool bRespectPerBucketLimit);

    /**
     * @brief 查询指定纯 UObject 类的池容量与活跃统计快照
     * @param ObjectClass 目标类
     * @return 池状态统计结构体
     */
    [[nodiscard]] FPoolStats GetPoolStats(TSubclassOf<UObject> ObjectClass) const;

    /**
     * @brief 从池中借出或构建纯 UObject 实例（支持可选类型指纹载荷，并在全部回调后校验重入有效性）
     * @param ObjectClass 目标类
     * @param Payload 可选业务载荷视图
     * @return 成功返回 UObject 指针，失败返回带 Game::ModuleId::Pool 的 Failure
     */
    TGameStatusResult<UObject*> AcquireObject(
        TSubclassOf<UObject> ObjectClass,
        FPoolAcquirePayload Payload = FPoolAcquirePayload()
    );

    /**
     * @brief 将已借出的纯 UObject 归还回池
     * @param Object 待归还的实例
     * @return 成功归还返回 Success，未借出或已销毁返回 Failure
     */
    TGameStatusResult<void> ReleaseObject(UObject* Object);

    /**
     * @brief 清空并回收指定纯 UObject 类别的所有空闲实例
     * @param ObjectClass 目标类
     */
    void ClearPool(TSubclassOf<UObject> ObjectClass);

    /**
     * @brief 清空并回收所有纯 UObject 类别的空闲实例
     */
    void ClearAllPools();

    /**
     * @brief 泄洪销毁所有类别的空闲 UObject 实例（从最冷端逐出，优先保留最热保底实例）
     * @param bPreserveMinCount 是否保留各池配置的 MinPreservedCount 保底数量
     * @return 实际销毁的实例数
     */
    int32 FlushIdleObjects(bool bPreserveMinCount = false);

    /**
     * @brief 查询指定 UObject 类别空闲池是否已达 MaxIdleCount 容量上限
     * @param ObjectClass 目标 UObject 类
     * @return 已达上限返回 true
     */
    [[nodiscard]] bool IsAtIdleCapacity(TSubclassOf<UObject> ObjectClass) const;

    /**
     * @brief 向 UE 垃圾回收系统注册 Outer、全部 Class Key、空闲与活跃 UObject 强引用
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
     * @brief 获取全部类别的活跃 UObject 实例总数
     * @return 活跃 UObject 实例总数
     */
    [[nodiscard]] int32 GetTotalActiveCount() const noexcept;

    /**
     * @brief 获取全部类别的空闲 UObject 实例总数
     * @return 空闲 UObject 实例总数
     */
    [[nodiscard]] int32 GetTotalIdleCount() const noexcept;

    /**
     * @brief 重置遥测统计计数器
     */
    void ResetTelemetry() noexcept;

private:
    void DiscardIdleObject(UObject* Object);
    void DestroyBucketIdleObjects(TPoolBucket<UObject>& Bucket);

private:
    TObjectPtr<UObject> OuterObject = nullptr;
    FPoolGenerationTracker* GenerationTracker = nullptr;
    TMap<TObjectPtr<UClass>, TPoolBucket<UObject>> ObjectPoolMap;

    int64 TotalAcquires = 0;
    int64 CacheHits = 0;
    int64 CacheMisses = 0;
};
