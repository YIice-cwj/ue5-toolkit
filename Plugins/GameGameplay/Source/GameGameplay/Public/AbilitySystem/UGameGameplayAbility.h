/**
 * @file UGameGameplayAbility.h
 * @brief GAS 能力基类（统一激活入口 + 激活否决守卫 + 预测边界护栏）
 *
 * 设计思路：框架层只固化"触发源记账 + 死亡/硬直 Tag 短路守卫"，零业务标签与数值
 * （具体否决标签由游戏层以编辑默认值配置）；网络分叉完全沿用 GAS 原生预测管线
 * （TryActivateAbility 本地预测 → ServerTryActivateAbility 服务器授权），
 * 基类不新增自定义 RPC，也不提供任何属性数值回滚接口——恪守"仅能力激活级轻预测"边界。
 */

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"

#include "GameGameplay.h"
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
 * @brief GAS 能力基类（游戏无关）
 */
UCLASS(Abstract)
class GAMEGAMEPLAY_API UGameGameplayAbility : public UGameplayAbility {
    GENERATED_BODY()

public:
    UGameGameplayAbility();

    bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

    void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /**
     * @brief 最近一次激活的触发来源（子类据以分叉表现层；已声明为复制属性，
     *        随能力实例复制到远端，故远端表现层可分叉）
     * @return 触发来源
     */
    [[nodiscard]] EGameAbilityTriggerSource GetTriggerSource() const noexcept;

protected:
    /** 激活否决标签：持有任一即短路激活（死亡 / 硬直等，具体标签由游戏层配置，框架零业务字面） */
    UPROPERTY(EditDefaultsOnly, Category = "Game|Ability")
    FGameplayTagContainer BlockingStateTags;

    /** 触发事件标签 → 触发来源映射（游戏层配置；未命中且带事件标签时按效果触发计） */
    UPROPERTY(EditDefaultsOnly, Category = "Game|Ability")
    TMap<FGameplayTag, EGameAbilityTriggerSource> TriggerSourceTagMap;

private:
    /**
     * @brief 由触发事件数据解析触发来源
     * @param InTriggerEventData 触发事件数据（为空即视为输入触发）
     * @return 触发来源
     *
     * 实现思路：空事件数据按输入触发计；带事件标签时先查映射，未命中回落效果触发
     */
    [[nodiscard]] EGameAbilityTriggerSource ResolveTriggerSource(const FGameplayEventData* InTriggerEventData) const;

    /** 最近一次激活的触发来源（默认按输入触发计；随能力实例复制，供客户端表现分叉） */
    UPROPERTY(Replicated)
    EGameAbilityTriggerSource TriggerSource = EGameAbilityTriggerSource::Input;
};
