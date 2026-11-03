/**
 * @file GameFeedbackReceiverComponent.h
 * @brief 网络回执接收组件（Client RPC → 本地总线）
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Network/GameFeedbackEventNet.h"

#include "GameFeedbackReceiverComponent.generated.h"

class APlayerController;

/**
 * @brief 服务端复制到 PlayerController、接收派发并回灌总线的桥接组件
 */
UCLASS(ClassGroup = (Game))
class GAMEFEEDBACK_API UGameFeedbackReceiverComponent : public UActorComponent {
    GENERATED_BODY()

public:
    /**
     * @brief 构造：关闭 Tick 并启用组件默认复制
     */
    UGameFeedbackReceiverComponent();

    /**
     * @brief 服务端调用：向指定客户端发送紧凑表现报文
     * @param Event 紧凑网络表现载荷
     */
    UFUNCTION(Client, Unreliable)
    void ClientReceiveFeedback(const FGameFeedbackEventNet& Event);

    /**
     * @brief 惰性确保目标 PlayerController 上存在接收组件（服务端创建后随组件复制同步到客户端）
     * @param PC 目标玩家控制器
     * @return 已存在或新建的接收组件指针；PC 为空时返回 nullptr
     */
    static UGameFeedbackReceiverComponent* EnsureOnPlayerController(APlayerController* PC);

private:
    /**
     * @brief 客户端接收实现：还原信号、判定预测抑制并回灌本地总线
     * @param Event 紧凑网络表现载荷
     */
    virtual void ClientReceiveFeedback_Implementation(const FGameFeedbackEventNet& Event);
};
