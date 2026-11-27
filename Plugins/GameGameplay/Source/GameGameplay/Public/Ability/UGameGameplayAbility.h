/**
 * @file UGameGameplayAbility.h
 * @brief GAS 能力基类（统一激活入口 + 激活否决守卫 + 零多余 Subobject 复制开销）
 *
 * 设计思路：框架层只固化"本地触发源记账 + 死亡/硬直状态标签短路守卫"，零业务标签与数值
 * （具体否决标签由游戏层以编辑默认值配置）；默认采用 InstancedPerActor + ReplicateNo，
 * 避免全项目所有能力产生不必要的 Subobject 复制通道开销，网络同步沿用 GAS 原生预测管线；
 * EndAbility 时自动复位触发源，杜绝 InstancedPerActor 跨次激活残留脏状态。
 */

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"

#include "UGameGameplayAbility.generated.h"

/**
 * @brief 能力触发来源（统一激活入口的三条来源）
 */
UENUM(BlueprintType)
enum class EGameAbilityTriggerSource : uint8 {
    Input,
    Effect,
    AI,
};

/**
 * @brief GAS 能力基类（抽象，游戏无关）
 *
 * 声明为 Abstract：基类仅固化触发源记账与阻断标签守卫，不承载任何可直接激活的完整能力，
 * 具体能力必须由游戏层派生实现；自动化测试若需实例化，须显式清除 CLASS_Abstract 后归还。
 */
UCLASS(Abstract)
class GAMEGAMEPLAY_API UGameGameplayAbility : public UGameplayAbility {
    GENERATED_BODY()

public:
    /**
     * @brief 构造函数：实例化策略设为 InstancedPerActor、复制策略设为 ReplicateNo（零额外 Subobject 通道开销）
     */
    UGameGameplayAbility();

    /**
     * @brief 通用激活否决守卫：引擎判定通过后追加死亡 / 硬直等状态标签短路并收集具体命中标签
     * @param Handle 能力规格句柄
     * @param ActorInfo 能力宿主信息
     * @param SourceTags 来源标签
     * @param TargetTags 目标标签
     * @param OptionalRelevantTags 可选相关标签输出（命中阻断标签时零临时容器拷贝逐项写入）
     * @return 允许激活返回 true
     */
    bool CanActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags = nullptr,
        const FGameplayTagContainer* TargetTags = nullptr,
        OUT FGameplayTagContainer* OptionalRelevantTags = nullptr
    ) const override;

    /**
     * @brief 统一激活入口：解析并记录本次触发来源后转交 GAS 原生激活路径
     * @param Handle 能力规格句柄
     * @param ActorInfo 能力宿主信息
     * @param ActivationInfo 激活信息（承载本地预测 / 服务器授权模式）
     * @param TriggerEventData 触发事件数据
     */
    void ActivateAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        const FGameplayEventData* TriggerEventData
    ) override;

    /**
     * @brief 能力结束回调：先转交基类完成结束回调（回调期内仍可读取本次触发来源），确认非活跃后再复位为 Input
     * @param Handle 能力规格句柄
     * @param ActorInfo 能力宿主信息
     * @param ActivationInfo 激活信息
     * @param bReplicateEndAbility 是否复制结束通知
     * @param bWasCancelled 是否因取消而结束
     */
    void EndAbility(
        const FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,
        bool bReplicateEndAbility,
        bool bWasCancelled
    ) override;

    /**
     * @brief 最近一次激活的触发来源（当前活跃期间有效，EndAbility 后复位为 Input · 强制内联）
     * @return 触发来源枚举值
     */
    [[nodiscard]] FORCEINLINE EGameAbilityTriggerSource GetTriggerSource() const noexcept {
        return TriggerSource;
    }

protected:
    /**
     * @brief 激活否决标签：持有任一即短路激活（死亡 / 硬直等，具体标签由游戏层配置，框架零业务字面）
     */
    UPROPERTY(EditDefaultsOnly, Category = "Game|Ability")
    FGameplayTagContainer BlockingStateTags;

    /**
     * @brief 触发事件标签到触发来源的映射配置（未命中且带事件标签时按效果触发计）
     */
    UPROPERTY(EditDefaultsOnly, Category = "Game|Ability")
    TMap<FGameplayTag, EGameAbilityTriggerSource> TriggerSourceTagMap;

private:
    /**
     * @brief 由触发事件数据解析触发来源（空事件按 Input 计，命中映射取配置值，未命中回落 Effect）
     * @param InTriggerEventData 触发事件数据（为空即视为输入触发）
     * @return 触发来源
     */
    [[nodiscard]] EGameAbilityTriggerSource ResolveTriggerSource(const FGameplayEventData* InTriggerEventData) const;

    /**
     * @brief 本次激活的触发来源（默认 Input，EndAbility 后自动复位）
     */
    EGameAbilityTriggerSource TriggerSource = EGameAbilityTriggerSource::Input;
};
