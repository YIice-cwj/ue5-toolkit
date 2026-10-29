/**
 * @file GameSaveArchive.h
 * @brief 通用存档归档对象（单一职责 · 承载槽位元信息 + 具名分段数据映射）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Save/GameSaveTypes.h"

#include "GameSaveArchive.generated.h"

/**
 * @brief 通用存档归档对象：承载槽位元信息 + 具名分段数据映射
 */
UCLASS(BlueprintType)
class GAMEDATACORE_API UGameSaveArchive : public USaveGame {
    GENERATED_BODY()

public:
    UGameSaveArchive() = default;

    /**
     * @brief 归档格式主版本常量
     */
    static constexpr int32 CurrentArchiveVersion = 1;

    /**
     * @brief 槽位元信息
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "GameData|Save")
    FGameSaveSlotMetadata Metadata;

    /**
     * @brief 分段名称到分段二进制负载的映射表
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameData|Save")
    TMap<FName, FGameSaveSegmentPayload> Segments;
};
