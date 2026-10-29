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
    /**
     * @brief 构造函数：实例化策略设为 InstancedPerActor、复制策略设为 ReplicateYes（能力实例与触发来源随 ASC 复制）
     */
    UGameGameplayAbility();

    /**
     * @brief 通用激活否决守卫：引擎判定通过后追加死亡 / 硬直等状态标签短路
     * @param Handle 能力规格句柄
     * @param ActorInfo 能力宿主信息
     * @param SourceTags 来源标签
     * @param TargetTags 目标标签
     * @param OptionalRelevantTags 可选相关标签输出
     * @return 允许激活返回 true
     */
    bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

    /**
     * @brief 统一激活入口：记录触发来源后转交 GAS 原生激活路径
     * @param Handle 能力规格句柄
     * @param ActorInfo 能力宿主信息
     * @param ActivationInfo 激活信息（承载本地预测 / 服务器授权模式）
     * @param TriggerEventData 触发事件数据
     */
    void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

    /**
     * @brief 注册网络复制属性：触发来源随能力实例复制到远端，供远端表现层分叉
     * @param OutLifetimeProps 输出属性列表
     */
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /**
     * @brief 最近一次激活的触发来源（子类据以分叉表现层；已声明为复制属性，随能力实例复制到远端）
     * @return 触发来源枚举值
     */
    [[nodiscard]] EGameAbilityTriggerSource GetTriggerSource() const noexcept;

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
     * @brief 由触发事件数据解析触发来源
     * @param InTriggerEventData 触发事件数据（为空即视为输入触发）
     * @return 触发来源
     *
     * 实现思路：空事件数据按输入触发计；带事件标签时先查映射，未命中回落效果触发
     */
    [[nodiscard]] EGameAbilityTriggerSource ResolveTriggerSource(const FGameplayEventData* InTriggerEventData) const;

    /**
     * @brief 最近一次激活的触发来源（默认按输入触发计；随能力实例复制，供客户端表现分叉）
     */
    UPROPERTY(Replicated)
    EGameAbilityTriggerSource TriggerSource = EGameAbilityTriggerSource::Input;
};
