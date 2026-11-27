/**
 * @file GameFeedbackHitStopService.h
 * @brief 顿帧全责服务（冷却、累积预算、全局定格与清除）
 */

#pragma once

#include "CoreMinimal.h"
#include "Bus/GameFeedbackMessages.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/PimplPtr.h"

#include "GameFeedbackHitStopService.generated.h"

struct FGameFeedbackHitStopAction;
struct FGameFeedbackHitStopServiceState;
class UGameFeedbackDefinition;

/**
 * @brief 订阅表现总线并统一应用/清除顿帧的世界服务
 */
UCLASS()
class GAMEFEEDBACK_API UGameFeedbackHitStopService : public UWorldSubsystem {
    GENERATED_BODY()

public:
    /**
     * @brief 构造：仅在非 CDO 实例装配内部状态（CDO 零堆分配）
     */
    UGameFeedbackHitStopService();

    /**
     * @brief 析构：通过类型擦除释放内部状态
     */
    ~UGameFeedbackHitStopService() override;

    /**
     * @brief 仅在游戏世界创建（包含 Dedicated Server）
     * @param Outer 外层对象
     * @return 是否创建
     */
    bool ShouldCreateSubsystem(UObject* Outer) const override;

    /**
     * @brief 世界 BeginPlay：订阅 Play / Network / Clear 通道
     * @param InWorld 当前世界
     *
     * 订阅必须晚于 GameInstance 子系统创建：世界子系统在 Initialize 阶段先于 GameInstance
     * 子系统集合初始化，此时访问 UGameplayMessageSubsystem 会因单例尚未就绪而断言失败。
     */
    void OnWorldBeginPlay(UWorld& InWorld) override;

    /**
     * @brief 注销：退订全部通道并清空限流状态
     */
    void Deinitialize() override;

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
     * @brief 清除指定实体顿帧状态回调
     * @param Channel 广播通道
     * @param Message 清除载荷
     */
    void OnClear(FGameplayTag Channel, const FGameFeedbackClearMessage& Message);

    /**
     * @brief 应用一次本地顿帧（含视角分流与累积预算）
     * @param Signal 表现信号
     * @param OverrideDefinition 覆盖资产；为空时按注册表解析
     */
    void ApplyHitStop(const FGameFeedbackSignal& Signal, const UGameFeedbackDefinition* OverrideDefinition);

    /**
     * @brief 判断本次命中是否应为攻击者应用卡肉顿帧（排除自伤双重顿帧，并对称支持分屏多玩家）
     * @param World 当前世界
     * @param Signal 表现信号
     * @param AttackerActor 攻击者实体
     * @param TargetActor 受击者实体
     * @return 应应用攻击者顿帧返回 true
     */
    [[nodiscard]] bool ShouldApplyAttackerHitStop(
        const UWorld& World,
        const FGameFeedbackSignal& Signal,
        const AActor* AttackerActor,
        const AActor* TargetActor) const;

    /**
     * @brief 为单个实体申请累积预算内的顿帧并提交给时间膨胀子系统
     * @param Actor 目标实体
     * @param RequestedDuration 期望持续时长（秒）
     * @param Action 顿帧参数
     * @param CurrentRealTime 当前真实时间（秒）
     */
    void ApplyActorHitStop(AActor* Actor, float RequestedDuration, const FGameFeedbackHitStopAction& Action, double CurrentRealTime);

    TPimplPtr<FGameFeedbackHitStopServiceState> State;
};
