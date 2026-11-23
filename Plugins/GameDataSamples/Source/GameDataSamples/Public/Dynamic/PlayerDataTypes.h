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

    /**
     * @brief 玩家当前等级
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Progression")
    int32 Level = 1;

    /**
     * @brief 玩家累计经验值
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Progression")
    int64 Experience = 0;

    /**
     * @brief 玩家当前持有金币数
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Progression")
    int64 Gold = 0;
};

/**
 * @brief 玩家全量动态持久化档案
 */
USTRUCT(BlueprintType)
struct GAMEDATASAMPLES_API FPlayerProfileData {
    GENERATED_BODY()

    /**
     * @brief 玩家唯一标识字符串
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Profile")
    FString PlayerId;

    /**
     * @brief 玩家成长数值子结构
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Profile")
    FPlayerProgressionData Progression;

    /**
     * @brief 玩家档案数据结构版本号
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Profile")
    int32 ArchiveVersion = 1;
};

