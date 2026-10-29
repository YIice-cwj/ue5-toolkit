/**
 * @file ANetSmokeGameMode.h
 * @brief 冒烟 GameMode（指定冒烟 Pawn 与控制器）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "ANetSmokeGameMode.generated.h"

/**
 * @brief 冒烟 GameMode（指定冒烟 Pawn 与控制器）
 */
UCLASS()
class ANetSmokeGameMode : public AGameModeBase {
    GENERATED_BODY()

public:
    /**
     * @brief 构造函数：指定冒烟 Pawn 与冒烟控制器类
     */
    ANetSmokeGameMode();
};

