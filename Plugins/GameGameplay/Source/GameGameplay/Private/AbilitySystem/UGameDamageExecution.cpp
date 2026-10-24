#include "AbilitySystem/UGameDamageExecution.h"

#include "AbilitySystem/FGameDamageChannelRegistry.h"
#include "AbilitySystem/FGameGameplayEffectContext.h"
#include "AbilitySystemComponent.h"
#include "Logging/GameLogCategories.h"

void UGameDamageExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const {
    TArray<FDamageChannelBinding> ChannelBindings;
    FGameDamageChannelRegistry::Get().GetBindings(GetClass(), ChannelBindings);
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
    }

    if (EffectContext != nullptr) {
        EffectContext->SetActualDamage(TotalCalculatedDamage);
        OnPostExecuteChannels(ExecutionParams, TotalCalculatedDamage, EffectContext);
    }
}

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
    return true;
}

bool UGameDamageExecution::UnregisterRuntimeChannelBinding(UClass* InExecutionClass, const FGameplayTag& InChannelTag) {
    return FGameDamageChannelRegistry::Get().Unregister(InExecutionClass, InChannelTag);
}

void UGameDamageExecution::RegisterTargetAttribute(const FGameplayAttribute& InAttribute, FGameplayEffectAttributeCaptureDefinition& OutDefinition, bool bSnapshot) {
    OutDefinition = FGameplayEffectAttributeCaptureDefinition(InAttribute, EGameplayEffectAttributeCaptureSource::Target, bSnapshot);
    RelevantAttributesToCapture.Add(OutDefinition);
}

void UGameDamageExecution::RegisterSourceAttribute(const FGameplayAttribute& InAttribute, FGameplayEffectAttributeCaptureDefinition& OutDefinition, bool bSnapshot) {
    OutDefinition = FGameplayEffectAttributeCaptureDefinition(InAttribute, EGameplayEffectAttributeCaptureSource::Source, bSnapshot);
    RelevantAttributesToCapture.Add(OutDefinition);
}

void UGameDamageExecution::RegisterChannelBinding(const FGameplayTag& ChannelTag, const FGameplayAttribute& TargetAttribute, EGameplayModOp::Type ModOp) {
    if (!ChannelTag.IsValid() || !TargetAttribute.IsValid()) {
        return;
    }
    FGameDamageChannelRegistry::Get().Register(GetClass(), FDamageChannelBinding(ChannelTag, TargetAttribute, ModOp));
}

bool UGameDamageExecution::ShouldExecuteChannel(const FGameplayTag& ChannelTag, const FGameplayEffectCustomExecutionParameters& InExecutionParams) const {
    return InExecutionParams.GetOwningSpec().SetByCallerTagMagnitudes.Contains(ChannelTag);
}

float UGameDamageExecution::GetChannelBaseDamage(const FGameplayTag& ChannelTag, const FGameplayEffectCustomExecutionParameters& InExecutionParams) const {
    return InExecutionParams.GetOwningSpec().GetSetByCallerMagnitude(ChannelTag, false, 0.0f);
}

FDamageModifier UGameDamageExecution::ComputeChannelModifier(
    const FGameplayTag& ChannelTag,
    float BaseDamage,
    const FGameplayTagContainer& SourceTags,
    const FGameplayTagContainer& TargetTags,
    const FGameplayEffectCustomExecutionParameters& ExecutionParams
) const {
    return FDamageModifier();
}

void UGameDamageExecution::OnPostExecuteChannels(
    const FGameplayEffectCustomExecutionParameters& ExecutionParams,
    float TotalCalculatedDamage,
    FGameGameplayEffectContext* OutEffectContext
) const {
}
