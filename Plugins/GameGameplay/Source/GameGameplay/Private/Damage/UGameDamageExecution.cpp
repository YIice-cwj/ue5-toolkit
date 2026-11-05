/**
 * @file UGameDamageExecution.cpp
 * @brief 泛型伤害结算 ExecCalc 基类实现（Tag 通道编排 + 运行时通道注册 + Modifier 修饰管线）
 */

#include "Damage/UGameDamageExecution.h"

#include "Damage/FGameDamageChannelRegistry.h"
#include "Damage/FGameGameplayEffectContext.h"
#include "AbilitySystemComponent.h"
#include "Logging/GameLogCategories.h"

/**
 * @brief 按已注册通道执行属性捕获、伤害修饰与输出结算（零拷贝切片遍历通道）
 * @param ExecutionParams 当前 GameplayEffect 执行参数
 * @param OutExecutionOutput 接收各通道修饰结果的执行输出
 */
void UGameDamageExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const {
    FGameDamageChannelRegistry::Get().ForEachBinding(GetClass(), [this, &ExecutionParams, &OutExecutionOutput](TConstArrayView<FDamageChannelBinding> ChannelBindings) {
        if (ChannelBindings.IsEmpty()) {
            return;
        }

        const UAbilitySystemComponent* SourceAbilitySystemComponent = ExecutionParams.GetSourceAbilitySystemComponent();
        const UAbilitySystemComponent* TargetAbilitySystemComponent = ExecutionParams.GetTargetAbilitySystemComponent();
        FGameplayTagContainer SourceTags;
        if (SourceAbilitySystemComponent != nullptr) {
            SourceAbilitySystemComponent->GetOwnedGameplayTags(SourceTags);
        }
        FGameplayTagContainer TargetTags;
        if (TargetAbilitySystemComponent != nullptr) {
            TargetAbilitySystemComponent->GetOwnedGameplayTags(TargetTags);
        }

        FGameGameplayEffectContext* EffectContext = FGameGameplayEffectContext::ExtractEffectContext(ExecutionParams.GetOwningSpec().GetContext());
        if (EffectContext != nullptr) {
            EffectContext->SetFeedbackEventId(static_cast<uint16>(ExecutionParams.GetPredictionKey().Current));
        }
        float TotalCalculatedDamage = 0.0f;

        for (const FDamageChannelBinding& Binding : ChannelBindings) {
            if (!ShouldExecuteChannel(Binding.ChannelTag, ExecutionParams)) {
                continue;
            }

            const float BaseDamage = GetChannelBaseDamage(Binding.ChannelTag, ExecutionParams);
            const FDamageModifier Modifier = ComputeChannelModifier(Binding.ChannelTag, BaseDamage, SourceTags, TargetTags, ExecutionParams);
            const float FinalDamage = Modifier.Evaluate(BaseDamage);

            if (EffectContext != nullptr) {
                EffectContext->SetActualChannelDamage(Binding.ChannelTag, FinalDamage);
            }

            if (!FMath::IsNearlyZero(FinalDamage)) {
                OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(Binding.TargetAttribute, Binding.ModOp, FinalDamage));
                TotalCalculatedDamage += FinalDamage;
            }

            GAME_LOG(Verbose, TEXT("【伤害计算】通道 [%s]: 基础伤害=%.2f, 计算后最终伤害=%.2f, 目标属性=[%s]。"),
                *Binding.ChannelTag.ToString(), BaseDamage, FinalDamage, *Binding.TargetAttribute.GetName());
        }

        if (EffectContext != nullptr) {
            EffectContext->SetActualDamage(TotalCalculatedDamage);
            OnPostExecuteChannels(ExecutionParams, TotalCalculatedDamage, EffectContext);
        }

        GAME_LOG(Log, TEXT("【伤害计算】伤害计算执行完毕: 来源=[%s], 目标=[%s], 通道总计实际伤害=%.2f。"),
            *GetNameSafe(SourceAbilitySystemComponent ? SourceAbilitySystemComponent->GetOwner() : nullptr),
            *GetNameSafe(TargetAbilitySystemComponent ? TargetAbilitySystemComponent->GetOwner() : nullptr),
            TotalCalculatedDamage);
    });
}

/**
 * @brief 运行期注册通道绑定（任意系统可调用，线程安全）
 * @param InExecutionClass 归属的 ExecCalc 类
 * @param InChannelTag 伤害通道标签
 * @param InTargetAttribute 目标输出元属性
 * @param InModOp 修饰操作
 * @return 注册成功返回 true
 */
bool UGameDamageExecution::RegisterRuntimeChannelBinding(
    UClass* InExecutionClass,
    const FGameplayTag& InChannelTag,
    const FGameplayAttribute& InTargetAttribute,
    EGameplayModOp::Type InModOp
) {
    if (InExecutionClass == nullptr || !InChannelTag.IsValid() || !InTargetAttribute.IsValid()) {
        GAME_LOG(Warning, TEXT("【伤害通道】运行期注册被拒：ExecCalc 类、通道标签或目标属性无效。"));
        return false;
    }

    FGameDamageChannelRegistry::Get().Register(InExecutionClass, FDamageChannelBinding(InChannelTag, InTargetAttribute, InModOp));
    GAME_LOG(Log, TEXT("【伤害通道】成功注册运行期绑定: 类=[%s], 通道=[%s], 目标属性=[%s]。"),
        *InExecutionClass->GetName(), *InChannelTag.ToString(), *InTargetAttribute.GetName());
    return true;
}

/**
 * @brief 运行期注销通道绑定
 * @param InExecutionClass 归属的 ExecCalc 类
 * @param InChannelTag 伤害通道标签
 * @return 注销成功返回 true
 */
bool UGameDamageExecution::UnregisterRuntimeChannelBinding(UClass* InExecutionClass, const FGameplayTag& InChannelTag) {
    return FGameDamageChannelRegistry::Get().Unregister(InExecutionClass, InChannelTag);
}

/**
 * @brief 注册具体捕获属性（受击端属性）
 * @param InAttribute 目标属性
 * @param OutDefinition 子类成员捕获定义
 * @param bSnapshot 是否在 GE 施加前快照
 */
void UGameDamageExecution::RegisterTargetAttribute(const FGameplayAttribute& InAttribute, FGameplayEffectAttributeCaptureDefinition& OutDefinition, bool bSnapshot) {
    OutDefinition = FGameplayEffectAttributeCaptureDefinition(InAttribute, EGameplayEffectAttributeCaptureSource::Target, bSnapshot);
    RelevantAttributesToCapture.AddUnique(OutDefinition);
}

/**
 * @brief 注册攻击端具体捕获属性
 * @param InAttribute 目标属性
 * @param OutDefinition 子类成员捕获定义
 * @param bSnapshot 是否在 GE 施加前快照
 */
void UGameDamageExecution::RegisterSourceAttribute(const FGameplayAttribute& InAttribute, FGameplayEffectAttributeCaptureDefinition& OutDefinition, bool bSnapshot) {
    OutDefinition = FGameplayEffectAttributeCaptureDefinition(InAttribute, EGameplayEffectAttributeCaptureSource::Source, bSnapshot);
    RelevantAttributesToCapture.AddUnique(OutDefinition);
}

/**
 * @brief 注册通道与输出元属性的绑定关系
 * @param ChannelTag 伤害通道标签
 * @param TargetAttribute 目标输出元属性
 * @param ModOp 修饰操作
 */
void UGameDamageExecution::RegisterChannelBinding(const FGameplayTag& ChannelTag, const FGameplayAttribute& TargetAttribute, EGameplayModOp::Type ModOp) {
    if (!ChannelTag.IsValid() || !TargetAttribute.IsValid()) {
        return;
    }
    FGameDamageChannelRegistry::Get().Register(GetClass(), FDamageChannelBinding(ChannelTag, TargetAttribute, ModOp));
}

/**
 * @brief 判定指定通道是否应在本轮执行演算
 * @param ChannelTag 通道标签
 * @param InExecutionParams 结算参数
 * @return 若应执行演算返回 true
 */
bool UGameDamageExecution::ShouldExecuteChannel(const FGameplayTag& ChannelTag, const FGameplayEffectCustomExecutionParameters& InExecutionParams) const {
    return InExecutionParams.GetOwningSpec().SetByCallerTagMagnitudes.Contains(ChannelTag);
}

/**
 * @brief 获取通道基础伤害
 * @param ChannelTag 通道标签
 * @param InExecutionParams 结算参数
 * @return 基础伤害
 */
float UGameDamageExecution::GetChannelBaseDamage(const FGameplayTag& ChannelTag, const FGameplayEffectCustomExecutionParameters& InExecutionParams) const {
    return InExecutionParams.GetOwningSpec().GetSetByCallerMagnitude(ChannelTag, false, 0.0f);
}

/**
 * @brief 全功能通道修饰演算钩子（倍率 + 固定加减 + 覆写）
 * @param ChannelTag 当前通道
 * @param BaseDamage 基础伤害
 * @param SourceTags 攻击端标签集
 * @param TargetTags 受击端标签集
 * @param ExecutionParams 原始结算上下文
 * @return 伤害修饰结果
 */
FDamageModifier UGameDamageExecution::ComputeChannelModifier(
    const FGameplayTag& ChannelTag,
    float BaseDamage,
    const FGameplayTagContainer& SourceTags,
    const FGameplayTagContainer& TargetTags,
    const FGameplayEffectCustomExecutionParameters& ExecutionParams
) const {
    return FDamageModifier();
}

/**
 * @brief 结算完成后的自定义反馈注入钩子
 * @param ExecutionParams 原始结算上下文
 * @param TotalCalculatedDamage 本轮计算出的实际总伤害
 * @param OutEffectContext 打击反馈上下文
 */
void UGameDamageExecution::OnPostExecuteChannels(
    const FGameplayEffectCustomExecutionParameters& ExecutionParams,
    float TotalCalculatedDamage,
    FGameGameplayEffectContext* OutEffectContext
) const {
}
