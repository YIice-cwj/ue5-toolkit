// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Misc/EnumClassFlags.h"

#include "ObjectPoolTypes.generated.h"

/**
 * @brief 对象池归还 Actor 时的组件自动重置策略位掩码
 */
enum class EPoolResetFlags : uint16 {
    None = 0,
    Timers = 1 << 0,
    Physics = 1 << 1,
    Movement = 1 << 2,
    Audio = 1 << 3,
    FXSystem = 1 << 4,
    Animation = 1 << 5,
    PawnAndAI = 1 << 6,
    All = Timers | Physics | Movement | Audio | FXSystem | Animation | PawnAndAI
};
ENUM_CLASS_FLAGS(EPoolResetFlags)

/**
 * @brief 对象池容量与活跃实例统计快照
 */
USTRUCT(BlueprintType)
struct GAMEPOOL_API FPoolStats {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "GamePool|ObjectPool")
    int32 TotalCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GamePool|ObjectPool")
    int32 ActiveCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GamePool|ObjectPool")
    int32 IdleCount = 0;
};

/**
 * @brief 对象池运行时高层遥测与命中率统计快照
 */
USTRUCT(BlueprintType)
struct GAMEPOOL_API FPoolTelemetrySnapshot {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "GamePool|Telemetry")
    int64 TotalAcquires = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GamePool|Telemetry")
    int64 CacheHits = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GamePool|Telemetry")
    int64 CacheMisses = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GamePool|Telemetry")
    int32 TotalActiveCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GamePool|Telemetry")
    int32 TotalIdleCount = 0;

    /**
     * @brief 计算对象池借出命中率百分比（0.0 ~ 100.0%）
     * @return 命中率百分比，若总借出数为 0 则返回 0.0f
     */
    [[nodiscard]] float GetHitRatePercentage() const noexcept {
        if (TotalAcquires <= 0) {
            return 0.0f;
        }
        return static_cast<float>((static_cast<double>(CacheHits) / static_cast<double>(TotalAcquires)) * 100.0);
    }
};

/**
 * @brief 对象池闲置弹性缩容与冷数据淘汰配置
 */
USTRUCT(BlueprintType)
struct GAMEPOOL_API FPoolEvictionSettings {
    GENERATED_BODY()

    /**
     * @brief 是否启用闲置超时自动淘汰（默认关闭）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction")
    bool bEnableEviction = false;

    /**
     * @brief 实例闲置生存期（秒）。超过此时间未被借出的实例将被平滑淘汰（默认 60.0s）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "0.1"))
    float IdleTimeToLiveSeconds = 60.0f;

    /**
     * @brief 保底保留的最小空闲实例数（防止全部淘汰导致下次借出时冷启动，默认 0）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "0"))
    int32 MinPreservedCount = 0;

    /**
     * @brief 自动检测与平滑淘汰心跳周期（秒，默认 5.0s）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "0.1"))
    float CheckIntervalSeconds = 5.0f;

    /**
     * @brief 单步平滑销毁最大实例配额（默认每步销毁 2 个，防止单步 GC 尖刺）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "1"))
    int32 MaxEvictionsPerStep = 2;
};

/**
 * @brief 单个 Actor 类别的对象池空闲与活跃实例存储桶
 */
USTRUCT()
struct GAMEPOOL_API FPoolBucket {
    GENERATED_BODY()

    UPROPERTY()
    TArray<TObjectPtr<AActor>> IdleActors;

    TArray<double> IdleReturnTimestamps;

    UPROPERTY()
    TSet<TObjectPtr<AActor>> IdleActorSet;

    UPROPERTY()
    TSet<TObjectPtr<AActor>> ActiveActorSet;

    /**
     * @brief 获取当前存储桶的活跃实例总数
     * @return 活跃实例数
     */
    [[nodiscard]] int32 GetActiveCount() const noexcept {
        return ActiveActorSet.Num();
    }

    /**
     * @brief 单类对象池最大空闲缓存数（<=0 表示不限制容量，默认 100）
     */
    int32 MaxIdleCount = 100;

    /**
     * @brief 归还入池时执行的组件重置策略掩码（默认全量重置）
     */
    EPoolResetFlags ResetFlags = EPoolResetFlags::All;

    /**
     * @brief 闲置弹性缩容与冷数据淘汰策略配置
     */
    UPROPERTY(EditAnywhere, Category = "GamePool|Eviction")
    FPoolEvictionSettings EvictionSettings;
};

/**
 * @brief 单个非 Actor UObject 类别的对象池空闲与活跃实例存储桶
 */
USTRUCT()
struct GAMEPOOL_API FObjectPoolBucket {
    GENERATED_BODY()

    UPROPERTY()
    TArray<TObjectPtr<UObject>> IdleObjects;

    TArray<double> IdleReturnTimestamps;

    UPROPERTY()
    TSet<TObjectPtr<UObject>> IdleObjectSet;

    UPROPERTY()
    TSet<TObjectPtr<UObject>> ActiveObjectSet;

    /**
     * @brief 获取当前存储桶的活跃实例总数
     * @return 活跃实例数
     */
    [[nodiscard]] int32 GetActiveCount() const noexcept {
        return ActiveObjectSet.Num();
    }

    /**
     * @brief 单类对象池最大空闲缓存数（<=0 表示不限制容量，默认 100）
     */
    int32 MaxIdleCount = 100;

    /**
     * @brief 闲置弹性缩容与冷数据淘汰策略配置
     */
    UPROPERTY(EditAnywhere, Category = "GamePool|Eviction")
    FPoolEvictionSettings EvictionSettings;
};
