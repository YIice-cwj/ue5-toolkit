// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/ObjectPoolTypes.h"
#include "Handle/PoolGenerationTracker.h"
#include "Types/TGameResult.h"
#include "UObject/GCObject.h"
#include "UObject/Object.h"

/**
 * @brief 纯 UObject 对象池底层核心管理器（负责非 Actor 纯 UObject 的生成、GC 引用保护、休眠复用）
 */
class FUObjectPoolManager : public FGCObject {
public:
    /**
     * @brief 初始化 UObject 池管理器
     * @param InOuter 所属 Outer 对象（通常为 UObjectPoolSubsystem）
     * @param InGenTracker 世代代数追踪器指针
     */
    void Initialize(UObject* InOuter, FPoolGenerationTracker* InGenTracker);

    /**
     * @brief 同步预热指定纯 UObject 类别
     * @param ObjectClass 目标 UObject 类
     * @param PrewarmCount 预热数量
     */
    void Prewarm(TSubclassOf<UObject> ObjectClass, int32 PrewarmCount);

    /**
     * @brief 设置指定 UObject 类的最大空闲缓存容量
     * @param ObjectClass 目标类
     * @param InMaxIdleCount 最大空闲数（<=0 表示无限制）
     */
    void SetMaxCapacity(TSubclassOf<UObject> ObjectClass, int32 InMaxIdleCount);

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
     * @brief 执行超时冷 UObject 实例淘汰
     * @param NowSeconds 当前单调硬件时间戳（秒）
     * @param MaxEvictCount 单次最大销毁配额（防止单步 GC 顿卡）
     * @return 实际销毁的冷实例总数
     */
    int32 EvictExpiredObjects(double NowSeconds, int32 MaxEvictCount = 2);

    /**
     * @brief 查询指定纯 UObject 类的池容量与活跃统计快照
     * @param ObjectClass 目标类
     * @return 池状态统计结构体
     */
    [[nodiscard]] FPoolStats GetPoolStats(TSubclassOf<UObject> ObjectClass) const;

    /**
     * @brief 从池中借出或构建纯 UObject 实例
     * @param ObjectClass 目标类
     * @return 成功返回 UObject 指针，失败返回 Failure
     */
    TGameStatusResult<UObject*> AcquireObject(TSubclassOf<UObject> ObjectClass);

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
     * @brief 生成单个空闲 UObject 实例并放入空闲池
     * @param ObjectClass 目标类
     * @return 成功生成并入池返回 Success，失败返回 Failure
     */
    TGameStatusResult<void> SpawnIdleObjectToPool(TSubclassOf<UObject> ObjectClass);

    /**
     * @brief 销毁并标记回收指定存储桶中的全部空闲 UObject
     * @param Bucket 目标存储桶
     */
    void DestroyBucketIdleObjects(FObjectPoolBucket& Bucket);

    /**
     * @brief 泄洪销毁所有类别的空闲 UObject 实例
     * @param bPreserveMinCount 是否保留各池配置的保底留存数量
     * @return 实际销毁的实例数
     */
    int32 FlushIdleObjects(bool bPreserveMinCount = false);

    /**
     * @brief FGCObject 引用收集回调（保证池内空闲与活跃 UObject 绝不被 GC 误杀）
     * @param Collector 引用收集器
     */
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

    /**
     * @brief 获取 GC 引用者名称
     * @return 引用者字符串
     */
    virtual FString GetReferencerName() const override {
        return TEXT("FUObjectPoolManager");
    }

    /**
     * @brief 获取当前注册的纯 UObject 池类别总数
     * @return 池总数
     */
    [[nodiscard]] int32 GetPoolCount() const noexcept {
        return ObjectPoolMap.Num();
    }

    /**
     * @brief 获取累计借出总次数
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

    /**
     * @brief 查找指定类的 UObject 存储桶指针
     * @param RawClass 目标 UClass
     * @return 存储桶指针，不存在返回 nullptr
     */
    [[nodiscard]] const FObjectPoolBucket* FindBucket(const UClass* RawClass) const {
        return ObjectPoolMap.Find(RawClass);
    }

    /**
     * @brief 查询或添加指定类的 UObject 存储桶引用
     * @param RawClass 目标 UClass
     * @return 存储桶引用
     */
    FObjectPoolBucket& FindOrAddBucket(UClass* RawClass) {
        return ObjectPoolMap.FindOrAdd(RawClass);
    }

private:
    UObject* OuterObject = nullptr;
    FPoolGenerationTracker* GenerationTracker = nullptr;
    TMap<TObjectPtr<UClass>, FObjectPoolBucket> ObjectPoolMap;

    int64 TotalAcquires = 0;
    int64 CacheHits = 0;
    int64 CacheMisses = 0;
};
