// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ObjectPool/ObjectPoolTypes.h"
#include "ObjectPool/PoolGenerationTracker.h"
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
     * @brief 销毁并标记回收指定存储桶中的全部空闲 UObject
     * @param Bucket 目标存储桶
     */
    void DestroyBucketIdleObjects(FObjectPoolBucket& Bucket);

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
};
