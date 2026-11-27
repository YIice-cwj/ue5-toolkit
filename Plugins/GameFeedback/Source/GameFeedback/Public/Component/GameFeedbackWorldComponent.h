/**
 * @file GameFeedbackWorldComponent.h
 * @brief 世界空间表现组件（受击闪白 / 粒子 / 空间音效）
 */

#pragma once

#include "CoreMinimal.h"
#include "Bus/GameFeedbackMessages.h"
#include "Components/ActorComponent.h"
#include "Templates/PimplPtr.h"

#include "GameFeedbackWorldComponent.generated.h"

class UGameFeedbackDefinition;
struct FGameFeedbackWorldComponentState;

/**
 * @brief 挂载于 GameState 的世界空间表现监听者
 */
UCLASS(ClassGroup = (Game))
class GAMEFEEDBACK_API UGameFeedbackWorldComponent : public UActorComponent {
    GENERATED_BODY()

public:
    /**
     * @brief 构造：关闭 Tick 并按需装配内部状态（CDO 零堆分配）
     */
    UGameFeedbackWorldComponent();

    /**
     * @brief 析构：通过类型擦除释放内部状态
     */
    ~UGameFeedbackWorldComponent() override;

    /**
     * @brief 开始播放：订阅 Play / Network / Clear 通道
     */
    void BeginPlay() override;

    /**
     * @brief 结束播放：还原 Overlay 并退订通道
     * @param EndPlayReason 结束原因
     */
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    /**
     * @brief 确保非 CDO 实例已创建内部状态
     */
    void EnsureState();

    /**
     * @brief 本地表现广播回调
     * @param Channel 广播通道
     * @param Message 表现载荷
     */
    void OnPlay(FGameplayTag Channel, const FGameFeedbackMessage& Message);

    /**
     * @brief 网络回执回调（抑制或同帧重复时跳过，支持分屏交错到达去重）
     * @param Channel 广播通道
     * @param Message 网络载荷
     */
    void OnNetwork(FGameplayTag Channel, const FGameFeedbackNetworkMessage& Message);

    /**
     * @brief 清除指定实体表现回调
     * @param Channel 广播通道
     * @param Message 清除载荷
     */
    void OnClear(FGameplayTag Channel, const FGameFeedbackClearMessage& Message);

    /**
     * @brief 解析资产并执行世界空间表现
     * @param Signal 表现信号
     * @param OverrideDefinition 覆盖资产；为空时按注册表解析
     */
    void HandleSignal(const FGameFeedbackSignal& Signal, const UGameFeedbackDefinition* OverrideDefinition);

    TPimplPtr<FGameFeedbackWorldComponentState> State;
};
