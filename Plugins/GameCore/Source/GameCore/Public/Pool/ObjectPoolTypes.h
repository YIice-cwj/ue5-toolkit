// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "ObjectPoolTypes.generated.h"

/**
 * @brief 单个 Actor 类别的对象池空闲与活跃实例存储桶
 */
USTRUCT()
struct GAMECORE_API FPoolBucket {
    GENERATED_BODY()

    UPROPERTY()
    TArray<TObjectPtr<AActor>> IdleActors;

    UPROPERTY()
    TSet<TObjectPtr<AActor>> IdleActorSet;

    int32 ActiveCount = 0;

    /**
     * @brief 单类对象池最大空闲缓存数（<=0 表示不限制容量，默认 100）
     */
    int32 MaxIdleCount = 100;
};

