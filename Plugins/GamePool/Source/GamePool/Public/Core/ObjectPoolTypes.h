/**
 * @file ObjectPoolTypes.h
 * @brief GamePool 对外公共配置、策略位掩码与统计遥测快照数据结构定义
 */

#pragma once

#include "CoreMinimal.h"
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
 * @brief 单个对象池的容量与活跃实例统计快照
 */
USTRUCT(BlueprintType)
struct GAMEPOOL_API FPoolStats {
    GENERATED_BODY()

    /**
     * @brief 池管理的实例总数（活跃数 + 空闲数）
     */
    UPROPERTY(BlueprintReadOnly, Category = "GamePool|ObjectPool")
    int32 TotalCount = 0;

    /**
     * @brief 当前已借出处于活跃态的实例数
     */
    UPROPERTY(BlueprintReadOnly, Category = "GamePool|ObjectPool")
    int32 ActiveCount = 0;

    /**
     * @brief 当前在池中休眠待复用的空闲实例数
     */
    UPROPERTY(BlueprintReadOnly, Category = "GamePool|ObjectPool")
    int32 IdleCount = 0;
};

/**
 * @brief 对象池运行时全局遥测与命中率统计快照
 */
USTRUCT(BlueprintType)
struct GAMEPOOL_API FPoolTelemetrySnapshot {
    GENERATED_BODY()

    /**
     * @brief 累计成功借出总次数（CacheHits + CacheMisses）
     */
    UPROPERTY(BlueprintReadOnly, Category = "GamePool|Telemetry")
    int64 TotalAcquires = 0;

    /**
     * @brief 累计复用池内闲置实例的命中次数
     */
    UPROPERTY(BlueprintReadOnly, Category = "GamePool|Telemetry")
    int64 CacheHits = 0;

    /**
     * @brief 累计池空触发新生成的穿透次数
     */
    UPROPERTY(BlueprintReadOnly, Category = "GamePool|Telemetry")
    int64 CacheMisses = 0;

    /**
     * @brief 当前全部对象池活跃实例总数
     */
    UPROPERTY(BlueprintReadOnly, Category = "GamePool|Telemetry")
    int32 TotalActiveCount = 0;

    /**
     * @brief 当前全部对象池空闲实例总数
     */
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
 * @brief 对象池闲置弹性缩容与冷数据淘汰策略配置
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
     * @brief 实例闲置生存期（秒）。超过此时间未被借出的冷实例将被平滑淘汰（默认 60.0s）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "0.1"))
    float IdleTimeToLiveSeconds = 60.0f;

    /**
     * @brief 保底保留的最小空闲实例数（在 TTL 淘汰与保底泄洪时优先保留，防冷启动抖动，默认 0）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "0"))
    int32 MinPreservedCount = 0;

    /**
     * @brief 自动检测与平滑淘汰心跳周期（秒，多池并存时调度器取活跃池中的最小周期，默认 5.0s）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "0.1"))
    float CheckIntervalSeconds = 5.0f;

    /**
     * @brief 本池单步平滑销毁最大实例配额（防止单帧 GC 尖刺，默认每步最多销毁 2 个）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GamePool|Eviction", meta = (ClampMin = "1"))
    int32 MaxEvictionsPerStep = 2;
};
