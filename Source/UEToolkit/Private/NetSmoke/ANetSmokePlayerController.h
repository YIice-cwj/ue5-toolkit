/**
 * @file ANetSmokePlayerController.h
 * @brief 客户端轮询复制结果并落盘的冒烟控制器
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "ANetSmokePlayerController.generated.h"

/**
 * @brief 客户端轮询复制结果并落盘的冒烟控制器
 */
UCLASS()
class ANetSmokePlayerController : public APlayerController {
    GENERATED_BODY()

public:
    /**
     * @brief 仅客户端启动轮询定时器，记录起始时刻以便超时判定
     */
    virtual void BeginPlay() override;

    /**
     * @brief 停止轮询定时器并清理资源
     * @param EndPlayReason 结束原因
     */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void PollReplicatedBuff();
    void Finish(const FString& InResult);

    FTimerHandle PollTimer;
    double StartSeconds = 0.0;
};

