/**
 * @file FGameBuffInstance.h
 * @brief 运行中的 Buff 实例条目（FastArray 复制条目）
 */

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Buff/FGameBuffTypes.h"

#include "FGameBuffInstance.generated.h"

/**
 * @brief 运行中的 Buff 实例（FastArray 复制条目）
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameBuffInstance : public FFastArraySerializerItem {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Game|Buff")
    int32 InstanceId = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Buff")
    FGameBuffSpec Spec;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Buff")
    int32 Stacks = 1;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Buff")
    float RemainingSeconds = 0.0f;
};
