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

    /** 复位为默认状态（缩放 1.0、无剩余时长、未激活） */
    void Reset() noexcept {
        *this = FGlobalChannelState{};
    }

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Time")
    float TargetDilation = 1.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Time")
    float CurrentDilation = 1.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Time")
    float RemainingDuration = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Time")
    float TotalDuration = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Time")
    float BlendOutDuration = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Time")
    float RemainingBlendOut = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameCore|Time")
    bool bActive = false;
};

/**
 * @brief 单个 Actor HitStop 顿帧记录
 */
struct FActorHitStopState {
    TWeakObjectPtr<AActor> TargetActor;
    float OriginalCustomTimeDilation = 1.0f;
    float HitStopTimeDilation = 0.001f;
    float RemainingDuration = 0.0f;
};

