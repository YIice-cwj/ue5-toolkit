/**
 * @file UGameDamageExecution.h
 * @brief 泛型伤害结算 ExecCalc 基类（Tag 通道编排 + 运行时通道注册 + Modifier 修饰管线）
 *
 * 设计思路：框架层彻底消除对特定游戏业务元属性（如物理/术法）的硬编码绑定；
 * 通道绑定由 FGameDamageChannelRegistry 统一托管，子类可在构造期通过 RegisterChannelBinding 声明，
 * 外部系统可在运行期直接通过 FGameDamageChannelRegistry::Get() 动态增删；
 * 支持全功能 FDamageModifier 演算管线（倍率/固定加减/覆写），放开零/负基础值以原生支持治疗与清零覆写通道。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "GameplayTagContainer.h"

#include "Damage/FDamageModifier.h"
#include "UGameDamageExecution.generated.h"

struct FGameGameplayEffectContext;

/**
 * @brief 泛型伤害结算 ExecCalc 基类（游戏无关，零具体业务数值）
 */
UCLASS(Abstract)
class GAMEGAMEPLAY_API UGameDamageExecution : public UGameplayEffectExecutionCalculation {
    GENERATED_BODY()

public:
    /**
     * @brief 按已注册通道执行属性捕获、伤害修饰与输出结算
     * @param ExecutionParams 当前 GameplayEffect 执行参数
     * @param OutExecutionOutput 接收各通道修饰结果的执行输出
     */
    void Execute_Implementation(
        const FGameplayEffectCustomExecutionParameters& ExecutionParams,
        FGameplayEffectCustomExecutionOutput& OutExecutionOutput
    ) const override;

protected:
    /**
     * @brief 注册受击端捕获属性（防御、抗性等，通常在结算时刻读取最新值不快照）
     * @param InAttribute 目标属性（游戏层具体字段）
     * @param OutDefinition 子类成员捕获定义（供 Execute 内 Evaluate 取值）
     * @param bSnapshot 是否在 GE 施加前快照（默认 false）
     */
    void RegisterTargetAttribute(
        const FGameplayAttribute& InAttribute,
        FGameplayEffectAttributeCaptureDefinition& OutDefinition,
        bool bSnapshot = false
    );

    /**
     * @brief 注册攻击端捕获属性（攻击力、破甲等，通常在 GE 施加前快照）
     * @param InAttribute 来源属性（游戏层具体字段）
     * @param OutDefinition 子类成员捕获定义（供 Execute 内 Evaluate 取值）
     * @param bSnapshot 是否在 GE 施加前快照（默认 true）
     */
    void RegisterSourceAttribute(
        const FGameplayAttribute& InAttribute,
        FGameplayEffectAttributeCaptureDefinition& OutDefinition,
        bool bSnapshot = true
    );

    /**
     * @brief 注册通道与输出元属性的绑定关系（供子类构造期或成员调用，内部委托至 FGameDamageChannelRegistry）
     * @param ChannelTag 伤害通道标签
     * @param TargetAttribute 目标输出元属性
     * @param ModOp 修饰操作
     */
    void RegisterChannelBinding(
        const FGameplayTag& ChannelTag,
        const FGameplayAttribute& TargetAttribute,
        EGameplayModOp::Type ModOp = EGameplayModOp::Additive
    );

    /**
     * @brief 判定指定通道是否应在本轮执行演算
     * @param ChannelTag 通道标签
     * @param InExecutionParams 结算参数
     * @return 若应执行演算返回 true（默认检查 Spec 是否携带该通道的 SetByCaller 数值）
     */
    virtual bool ShouldExecuteChannel(
        const FGameplayTag& ChannelTag,
        const FGameplayEffectCustomExecutionParameters& InExecutionParams
    ) const;

    /**
     * @brief 获取通道基础伤害（默认从 SetByCaller 取值）
     * @param ChannelTag 通道标签
     * @param InExecutionParams 结算参数
     * @return 基础伤害
     */
    virtual float GetChannelBaseDamage(
        const FGameplayTag& ChannelTag,
        const FGameplayEffectCustomExecutionParameters& InExecutionParams
    ) const;

    /**
     * @brief 全功能通道修饰演算钩子（倍率 + 固定加减 + 覆写）
     * @param ChannelTag 当前通道
     * @param BaseDamage 基础伤害
     * @param SourceTags 攻击端聚合标签集（包含 Actor 与 GE/Spec 标签）
     * @param TargetTags 受击端聚合标签集（包含 Actor 与 GE/Spec 标签）
     * @param ExecutionParams 原始结算上下文
     * @return 伤害修饰结果
     */
    virtual FDamageModifier ComputeChannelModifier(
        const FGameplayTag& ChannelTag,
        float BaseDamage,
        const FGameplayTagContainer& SourceTags,
        const FGameplayTagContainer& TargetTags,
        const FGameplayEffectCustomExecutionParameters& ExecutionParams
    ) const;

    /**
     * @brief 结算完成后的自定义反馈注入钩子（仅当上下文为 FGameGameplayEffectContext 时触发）
     * @param ExecutionParams 原始结算上下文
     * @param TotalCalculatedDamage 本轮计算出的实际总伤害
     * @param OutEffectContext 打击反馈上下文（非空）
     */
    virtual void OnPostExecuteChannels(
        const FGameplayEffectCustomExecutionParameters& ExecutionParams,
        float TotalCalculatedDamage,
        FGameGameplayEffectContext* OutEffectContext
    ) const;
};
