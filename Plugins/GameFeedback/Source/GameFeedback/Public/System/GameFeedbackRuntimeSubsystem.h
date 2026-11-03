/**
 * @file GameFeedbackRuntimeSubsystem.h
 * @brief 组件装配子系统（仅接线，不含表现逻辑）
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"

#include "GameFeedbackRuntimeSubsystem.generated.h"

/**
 * @brief 为 GameState 与本地 PlayerController 惰性装配表现组件的接线器
 */
UCLASS()
class GAMEFEEDBACK_API UGameFeedbackRuntimeSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
    /**
     * @brief 仅保留游戏世界
     * @param Outer 外层对象
     * @return 是否创建
     */
    bool ShouldCreateSubsystem(UObject* Outer) const override;

    /**
     * @brief 世界 BeginPlay：装配现有组件的并启动迟到玩家补齐定时器
     * @param InWorld 当前世界
     */
    void OnWorldBeginPlay(UWorld& InWorld) override;

    /**
     * @brief 注销：停止补齐定时器
     */
    void Deinitialize() override;

    /**
     * @brief 确保 GameState、PlayerController 及网络组件按 NetMode 正确装配
     * @param World 当前世界
     */
    void EnsureComponents(UWorld& World);

private:
    FTimerHandle EnsureTimer;
};
