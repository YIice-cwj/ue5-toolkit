/**
 * @file GameFeedbackPlayerComponent.h
 * @brief 本地玩家表现组件（镜头震动 / 触觉 / 确认音 / 预测指纹）
 */

#pragma once

#include "CoreMinimal.h"
#include "Bus/GameFeedbackMessages.h"
#include "Components/ActorComponent.h"
#include "Templates/PimplPtr.h"

#include "GameFeedbackPlayerComponent.generated.h"

class APlayerController;
class UGameFeedbackDefinition;
struct FGameFeedbackCameraShakeAction;
struct FGameFeedbackForceFeedbackAction;
struct FGameFeedbackPlayerComponentState;

/**
 * @brief 挂载于本地 PlayerController 的玩家侧表现监听者
 */
UCLASS(ClassGroup = (Game))
class GAMEFEEDBACK_API UGameFeedbackPlayerComponent : public UActorComponent {
    GENERATED_BODY()

public:
    /**
     * @brief 构造：关闭 Tick 并按需装配内部状态（CDO 零堆分配）
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

    /**
     * @brief 查询当前活跃镜头震动追踪数量（供测试与可观测性）
     * @return 活跃追踪数量
     */
    [[nodiscard]] int32 GetTrackedShakeCount() const;

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
     * @brief 网络回执回调（抑制标记为真或定向至其他本地分屏控制器时跳过）
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

    /**
     * @brief 按观察视角执行镜头震动
     * @param LocalPC 本地玩家控制器
     * @param Perspective 观察视角
     * @param Signal 表现信号
     * @param Action 镜头震动参数
     */
    void ExecuteCameraShake(
        APlayerController& LocalPC,
        EGameFeedbackPerspective Perspective,
        const FGameFeedbackSignal& Signal,
        const FGameFeedbackCameraShakeAction& Action);

    /**
     * @brief 按观察视角与无障碍倍率执行手柄触觉震动
     * @param LocalPC 本地玩家控制器
     * @param Perspective 观察视角
     * @param Magnitude 表现强度
     * @param GlobalHapticScale 项目全局触觉缩放倍率
     * @param Action 触觉震动参数
     */
    static void ExecuteHaptics(
        APlayerController& LocalPC,
        EGameFeedbackPerspective Perspective,
        float Magnitude,
        float GlobalHapticScale,
        const FGameFeedbackForceFeedbackAction& Action);

    TPimplPtr<FGameFeedbackPlayerComponentState> State;
};
