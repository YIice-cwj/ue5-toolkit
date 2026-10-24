/**
 * @file PlayerDataTypes.h
 * @brief 玩家持久化数据类型定义
 */

#pragma once

#include "CoreMinimal.h"
#include "PlayerDataTypes.generated.h"

/**
 * @brief 玩家持久化基础成长数据
 */
USTRUCT(BlueprintType)
struct GAMEDATASAMPLES_API FPlayerProgressionData {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Progression")
    int32 Level = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Progression")
    int64 Experience = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Progression")
    int64 Gold = 0;
};

/**
 * @brief 玩家全量动态持久化档案
 */
USTRUCT(BlueprintType)
struct GAMEDATASAMPLES_API FPlayerProfileData {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Profile")
    FString PlayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Profile")
    FPlayerProgressionData Progression;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Profile")
    int32 ArchiveVersion = 1;
};

