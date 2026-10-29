// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TimeDilationTypes.generated.h"

/**
 * @brief 全局时间膨胀通道枚举（数值越小优先级越高）
 */
UENUM(BlueprintType)
enum class ETimeDilationChannel : uint8 {
    SystemPause = 0     UMETA(DisplayName = "System Pause"),
    HitStop = 1         UMETA(DisplayName = "Hit Stop"),
    SlowMo = 2          UMETA(DisplayName = "Slow Motion"),
    WorldModifier = 3   UMETA(DisplayName = "World Modifier"),
    Count = 4           UMETA(Hidden)
};

/**
 * @brief 全局时间膨胀通道运行时状态
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FGlobalChannelState {
    GENERATED_BODY()

    /**
     * @brief 复位为默认初始状态（缩放比 1.0、无剩余时长、未激活）
     */
    void Reset() noexcept {
        *this = FGlobalChannelState{};
    }

    /**
     * @brief 目标时间缩放倍率
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Time")
    float TargetDilation = 1.0f;

    /**
     * @brief 当前插值或生效中的时间缩放倍率
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Time")
    float CurrentDilation = 1.0f;

    /**
     * @brief 当前阶段剩余持续时间（秒）
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Time")
    float RemainingDuration = 0.0f;

    /**
     * @brief 总持续时间（秒）
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Time")
    float TotalDuration = 0.0f;

    /**
     * @brief 恢复正常时间时的平滑淡出时长（秒）
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Time")
    float BlendOutDuration = 0.0f;

    /**
     * @brief 淡出阶段剩余时长（秒）
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Time")
    float RemainingBlendOut = 0.0f;

    /**
     * @brief 通道当前是否处于激活状态
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Time")
    bool bActive = false;
};

/**
 * @brief 单个 Actor HitStop 顿帧记录
 */
struct FActorHitStopState {
    /**
     * @brief 目标 Actor 弱指针
     */
    TWeakObjectPtr<AActor> TargetActor;

    /**
     * @brief 进入顿帧前的原始自定义时间膨胀率
     */
    float OriginalCustomTimeDilation = 1.0f;

    /**
     * @brief 顿帧期间的目标局部时间膨胀率
     */
    float HitStopTimeDilation = 0.001f;

    /**
     * @brief 顿帧剩余持续时间（秒）
     */
    float RemainingDuration = 0.0f;
};
