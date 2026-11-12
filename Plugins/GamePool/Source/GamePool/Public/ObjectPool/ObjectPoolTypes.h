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
 * @brief 单个 Actor 类别的对象池空闲与活跃实例存储桶
 */
USTRUCT()
struct GAMEPOOL_API FPoolBucket {
    GENERATED_BODY()

    UPROPERTY()
    TArray<TObjectPtr<AActor>> IdleActors;

    UPROPERTY()
    TSet<TObjectPtr<AActor>> IdleActorSet;

    UPROPERTY()
    TSet<TObjectPtr<AActor>> ActiveActorSet;

    int32 ActiveCount = 0;

    /**
     * @brief 单类对象池最大空闲缓存数（<=0 表示不限制容量，默认 100）
     */
    int32 MaxIdleCount = 100;

    /**
     * @brief 归还入池时执行的组件重置策略掩码（默认全量重置）
     */
    EPoolResetFlags ResetFlags = EPoolResetFlags::All;
};

/**
 * @brief 单个非 Actor UObject 类别的对象池空闲与活跃实例存储桶
 */
USTRUCT()
struct GAMEPOOL_API FObjectPoolBucket {
    GENERATED_BODY()

    UPROPERTY()
    TArray<TObjectPtr<UObject>> IdleObjects;

    UPROPERTY()
    TSet<TObjectPtr<UObject>> IdleObjectSet;

    UPROPERTY()
    TSet<TObjectPtr<UObject>> ActiveObjectSet;

    int32 ActiveCount = 0;

    /**
     * @brief 单类对象池最大空闲缓存数（<=0 表示不限制容量，默认 100）
     */
    int32 MaxIdleCount = 100;
};
