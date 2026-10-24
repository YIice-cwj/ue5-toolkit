/**
 * @file NetSmokeHarness.h
 * @brief P7.5 网络链路冒烟：服务端施加 Buff，客户端确认状态标签经复制到达
 *
 * 该装置仅用于烟测，不参与任何运行时玩法。服务端在 Pawn 上施加带状态标签的 Buff，
 * 客户端轮询自身 Pawn 的授予标签，命中即写结果文件并退出；结果文件是 Shipping 裁剪日志、
 * 且 macOS 忽略退出码时唯一可靠的取证通道。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#include "NetSmokeHarness.generated.h"

class UGameBuffComponent;

/**
 * @brief 冒烟结果落盘到 Saved/NetSmoke 目录
 * @param InFileName 结果文件名
 * @param InContent 结果内容
 */
namespace NetSmokeResult {
    void Write(const TCHAR* InFileName, const FString& InContent);
}

/**
 * @brief 携带 Buff 组件的冒烟 Pawn（服务端施加 Buff）
 */
UCLASS()
class ANetSmokePawn : public APawn {
    GENERATED_BODY()

public:
    ANetSmokePawn();

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(VisibleAnywhere, Category = "NetSmoke")
    TObjectPtr<UGameBuffComponent> BuffComponent;
};

/**
 * @brief 客户端轮询复制结果并落盘的冒烟控制器
 */
UCLASS()
class ANetSmokePlayerController : public APlayerController {
    GENERATED_BODY()

public:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void PollReplicatedBuff();
    void Finish(const FString& InResult);

    FTimerHandle PollTimer;
    double StartSeconds = 0.0;
};

/**
 * @brief 冒烟 GameMode（指定冒烟 Pawn 与控制器）
 */
UCLASS()
class ANetSmokeGameMode : public AGameModeBase {
    GENERATED_BODY()

public:
    ANetSmokeGameMode();
};
