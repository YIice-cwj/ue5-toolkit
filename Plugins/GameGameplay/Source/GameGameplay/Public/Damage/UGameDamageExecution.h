/**
 * @file UGameDamageExecution.h
 * @brief 泛型伤害结算 ExecCalc 基类（Tag 通道编排 + 运行时通道注册 + Modifier 修饰管线）
 *
 * 设计思路：框架层彻底消除对特定游戏业务元属性（如物理/术法）的硬编码绑定；
 * 采用通道绑定注册机制（RegisterChannelBinding / RegisterRuntimeChannelBinding），按 GameplayTag 自动遍历分发输出；
 * 支持全功能 FDamageModifier 演算管线（倍率/固定加减/覆写），放开零/负基础值以原生支持治疗通道；
 * 绑定关系由 FGameDamageChannelRegistry 统一托管，可在 CDO 构造期声明，也可在运行期动态增删。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GameplayEffectExecutionCalculation.h"
#include "GameplayTagContainer.h"

#include "GameGameplay.h"
#include "Damage/FDamageModifier.h"
#include "UGameDamageExecution.generated.h"

/**
 * @brief 泛型伤害结算 ExecCalc 基类（游戏无关，零具体业务数值）
 */
UCLASS(Abstract)
class GAMEGAMEPLAY_API UGameDamageExecution : public UGameplayEffectExecutionCalculation {
    GENERATED_BODY()

public:
    void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;

    /**
     * @brief 运行期注册通道绑定（任意系统可调用，线程安全）
     * @param InExecutionClass 归属的 ExecCalc 类
     * @param InChannelTag 伤害通道标签
     * @param InTargetAttribute 目标输出元属性
     * @param InModOp 修饰操作
     * @return 注册成功返回 true
     */
    static bool RegisterRuntimeChannelBinding(
        UClass* InExecutionClass,
        const FGameplayTag& InChannelTag,
        const FGameplayAttribute& InTargetAttribute,
        EGameplayModOp::Type InModOp = EGameplayModOp::Additive
    );

    /**
     * @brief 运行期注销通道绑定
     * @param InExecutionClass 归属的 ExecCalc 类
     * @param InChannelTag 伤害通道标签
     * @return 注销成功返回 true
     */
    static bool UnregisterRuntimeChannelBinding(UClass* InExecutionClass, const FGameplayTag& InChannelTag);

protected:
    /**
     * @brief 注册具体捕获属性（基类只感知通用元属性，业务字段由子类在构造函数一行注册）
     * @param InAttribute 目标属性（游戏层具体字段）
     * @param OutDefinition 子类成员捕获定义（供 Execute 内 Evaluate 取值）
     * @param bSnapshot 是否在 GE 施加前快照（受击方防御类属性通常不快照，传 false）
     */
    void RegisterTargetAttribute(const FGameplayAttribute& InAttribute, FGameplayEffectAttributeCaptureDefinition& OutDefinition, bool bSnapshot = false);

    /**
     * @brief 注册攻击端具体捕获属性（攻击力、破甲等）
     * @param InAttribute 目标属性（游戏层具体字段）
     * @param OutDefinition 子类成员捕获定义（供 Execute 内 Evaluate 取值）
     * @param bSnapshot 是否在 GE 施加前快照（通常攻击端在施加前快照）
     */
    void RegisterSourceAttribute(const FGameplayAttribute& InAttribute, FGameplayEffectAttributeCaptureDefinition& OutDefinition, bool bSnapshot = false);

    /**
     * @brief 注册通道与输出元属性的绑定关系（可在子类构造函数或运行期调用，线程安全）
     * @param ChannelTag 伤害通道标签
     * @param TargetAttribute 目标输出元属性
     * @param ModOp 修饰操作
     */
    void RegisterChannelBinding(const FGameplayTag& ChannelTag, const FGameplayAttribute& TargetAttribute, EGameplayModOp::Type ModOp = EGameplayModOp::Additive);

    /**
     * @brief 判定指定通道是否应在本轮执行演算
     * @param ChannelTag 通道标签
     * @param InExecutionParams 结算参数
     * @return 若应执行演算返回 true（默认检查 Spec 是否携带该通道的 SetByCaller 数值）
     */
    virtual bool ShouldExecuteChannel(const FGameplayTag& ChannelTag, const FGameplayEffectCustomExecutionParameters& InExecutionParams) const;

    /**
     * @brief 获取通道基础伤害（默认从 SetByCaller 取值）
     * @param ChannelTag 通道标签
     * @param InExecutionParams 结算参数
     * @return 基础伤害
     */
    virtual float GetChannelBaseDamage(const FGameplayTag& ChannelTag, const FGameplayEffectCustomExecutionParameters& InExecutionParams) const;

    /**
     * @brief 全功能通道修饰演算钩子（倍率 + 固定加减 + 覆写）
     * @param ChannelTag 当前通道
     * @param BaseDamage 基础伤害
     * @param SourceTags 攻击端标签集
     * @param TargetTags 受击端标签集
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
     * @brief 结算完成后的自定义反馈注入钩子（供子类根据战斗判定追加暴击、硬直等反馈标签或动态数值）
     * @param ExecutionParams 原始结算上下文
     * @param TotalCalculatedDamage 本轮计算出的实际总伤害
     * @param OutEffectContext 打击反馈上下文
     */
    virtual void OnPostExecuteChannels(
        const FGameplayEffectCustomExecutionParameters& ExecutionParams,
        float TotalCalculatedDamage,
        struct FGameGameplayEffectContext* OutEffectContext
    ) const;
};
