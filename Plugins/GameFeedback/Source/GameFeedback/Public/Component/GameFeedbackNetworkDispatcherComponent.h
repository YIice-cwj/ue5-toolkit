/**
 * @file GameFeedbackNetworkDispatcherComponent.h
 * @brief 权威端网络派发组件（视距裁剪与每帧限流）
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Templates/PimplPtr.h"
#include "Bus/GameFeedbackMessages.h"

#include "GameFeedbackNetworkDispatcherComponent.generated.h"

struct FGameFeedbackNetworkDispatcherComponentState;

/**
 * @brief 挂载于 GameState 的权威端派发监听者
 */
UCLASS(ClassGroup = (Game))
class GAMEFEEDBACK_API UGameFeedbackNetworkDispatcherComponent : public UActorComponent {
    GENERATED_BODY()

public:
    /**
     * @brief 构造：装配派发限流状态
     */
    UGameFeedbackNetworkDispatcherComponent();

    /**
     * @brief 析构：通过类型擦除释放内部状态
     */
    ~UGameFeedbackNetworkDispatcherComponent() override;

    /**
     * @brief 开始播放：订阅 Play 通道
     */
    void BeginPlay() override;

    /**
     * @brief 结束播放：退订通道
     * @param EndPlayReason 结束原因
     */
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    /**
     * @brief 本地表现广播回调（仅权威端响应）
     * @param Channel 广播通道
     * @param Message 表现载荷
     */
    void OnPlay(FGameplayTag Channel, const FGameFeedbackMessage& Message);

    /**
     * @brief 按服务端最终 Definition 身份向视距内远端客户端派发紧凑报文
     * @param Message 本地表现消息（含可选 OverrideDefinition）
     */
    void Dispatch(const FGameFeedbackMessage& Message);

    TPimplPtr<FGameFeedbackNetworkDispatcherComponentState> State;
};
