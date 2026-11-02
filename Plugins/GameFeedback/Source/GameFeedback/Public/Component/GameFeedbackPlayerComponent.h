/**
 * @file GameFeedbackPlayerComponent.h
 * @brief 本地玩家表现组件（镜头震动 / 触觉 / 确认音 / 预测指纹）
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Templates/PimplPtr.h"
#include "Bus/GameFeedbackMessages.h"

#include "GameFeedbackPlayerComponent.generated.h"

struct FGameFeedbackPlayerComponentState;
class UGameFeedbackDefinition;

/**
 * @brief 挂载于本地 PlayerController 的玩家侧表现监听者
 */
UCLASS(ClassGroup = (Game))
class GAMEFEEDBACK_API UGameFeedbackPlayerComponent : public UActorComponent {
    GENERATED_BODY()

public:
    /**
     * @brief 构造：装配镜头震动与预测状态
     */
    UGameFeedbackPlayerComponent();

    /**
     * @brief 析构：通过类型擦除释放内部状态
     */
    ~UGameFeedbackPlayerComponent() override;

    /**
     * @brief 开始播放：订阅 Play / Network 通道
     */
    void BeginPlay() override;

    /**
     * @brief 结束播放：停止震动并退订通道
     * @param EndPlayReason 结束原因
     */
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /**
     * @brief 消费一条本地预测指纹（供网络接收组件判定是否抑制回执）
     * @param Signal 网络还原后的表现信号
     * @return 命中并消费返回 true
     */
    bool ConsumePrediction(const FGameFeedbackSignal& Signal);

private:
    /**
     * @brief 本地表现广播回调
     * @param Channel 广播通道
     * @param Message 表现载荷
     */
    void OnPlay(FGameplayTag Channel, const FGameFeedbackMessage& Message);

    /**
     * @brief 网络回执回调（抑制标记为真时跳过）
     * @param Channel 广播通道
     * @param Message 网络载荷
     */
    void OnNetwork(FGameplayTag Channel, const FGameFeedbackNetworkMessage& Message);

    /**
     * @brief 解析视角并执行玩家侧表现
     * @param Signal 表现信号
     * @param OverrideDefinition 覆盖资产；为空时按注册表解析
     * @param bFromNetwork 是否来自网络回执
     */
    void HandleSignal(const FGameFeedbackSignal& Signal, const UGameFeedbackDefinition* OverrideDefinition, bool bFromNetwork);

    TPimplPtr<FGameFeedbackPlayerComponentState> State;
};
