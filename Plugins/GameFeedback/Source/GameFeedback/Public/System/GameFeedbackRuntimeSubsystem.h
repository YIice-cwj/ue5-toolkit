/**
 * @file GameFeedbackRuntimeSubsystem.h
 * @brief 组件装配子系统（仅接线，不含表现逻辑）
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Subsystems/WorldSubsystem.h"

#include "GameFeedbackRuntimeSubsystem.generated.h"

class AGameModeBase;
class APlayerController;
class ULocalPlayer;

/**
 * @brief 为 GameState 与本地/远端 PlayerController 按 NetMode 惰性装配表现组件的接线器
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
     * @brief 世界 BeginPlay：装配现有组件并注册服务端登录与本地分屏加入事件
     * @param InWorld 当前世界
     */
    void OnWorldBeginPlay(UWorld& InWorld) override;

    /**
     * @brief 注销：停止定时器并退订服务端登录与本地玩家加入事件
     */
    void Deinitialize() override;

    /**
     * @brief 确保 GameState、PlayerController 及网络组件按 NetMode 正确装配
     * @param World 当前世界
     * @return 若当前所需组件均已就绪返回 true；若存在待装配对象返回 false
     */
    bool EnsureComponents(UWorld& World);

private:
    /**
     * @brief 启动延迟补齐定时器，直至全部必备组件装配就绪后自动停止
     * @param World 当前世界
     */
    void StartEnsureTimer(UWorld& World);

    /**
     * @brief 服务端新玩家登录回调：过滤世界与本地控制器后立即装配网络接收组件
     * @param GameMode 当前游戏模式
     * @param NewPlayer 新登录的玩家控制器
     */
    void OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);

    /**
     * @brief 本地分屏玩家新增回调：触发该玩家的 PlayerComponent 装配或重启延迟补齐
     * @param NewLocalPlayer 新加入的本地玩家
     */
    void OnLocalPlayerAdded(ULocalPlayer* NewLocalPlayer);

    FTimerHandle EnsureTimer;
    FDelegateHandle PostLoginDelegateHandle;
    FDelegateHandle LocalPlayerAddedDelegateHandle;
};
