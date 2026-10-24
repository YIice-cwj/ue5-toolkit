/**
 * @file GameSaveTestTypes.h
 * @brief 存档子系统自动化测试专用 USaveGame 派生类
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"

#include "GameSaveTestTypes.generated.h"

/**
 * @brief 存档子系统测试用自定义存档对象（验证任意 USaveGame 类的存/取逃生口）
 */
UCLASS()
class GAMEDATACORE_API UGameSaveTestArchive : public USaveGame {
    GENERATED_BODY()

public:
    UPROPERTY()
    int32 Value = 0;

    UPROPERTY()
    FString Label;
};
