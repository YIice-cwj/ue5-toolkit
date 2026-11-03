/**
 * @file UGameDamageExecutionTest.cpp
 * @brief 泛型伤害结算 ExecCalc 基类自动化测试套件
 */

#include "Damage/UGameDamageExecution.h"

#if WITH_AUTOMATION_TESTS

#include "Ability/UGameAbilitySystemComponent.h"
#include "AbilitySystemTestAttributeSet.h"
#include "Damage/FGameDamageChannelRegistry.h"
#include "Damage/FGameGameplayEffectContext.h"
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"

namespace {

class FTestDamageExecutionProxy : public UGameDamageExecution {
public:
    static void TestRegisterTargetAttribute(UGameDamageExecution* Target, const FGameplayAttribute& InAttribute, FGameplayEffectAttributeCaptureDefinition& OutDefinition, bool bSnapshot) {
        static_cast<FTestDamageExecutionProxy*>(Target)->RegisterTargetAttribute(InAttribute, OutDefinition, bSnapshot);
    }

    static void TestRegisterSourceAttribute(UGameDamageExecution* Target, const FGameplayAttribute& InAttribute, FGameplayEffectAttributeCaptureDefinition& OutDefinition, bool bSnapshot) {
        static_cast<FTestDamageExecutionProxy*>(Target)->RegisterSourceAttribute(InAttribute, OutDefinition, bSnapshot);
    }

    static void ResetCapturedAttributes(UGameDamageExecution* Target) {
        static_cast<FTestDamageExecutionProxy*>(Target)->RelevantAttributesToCapture.Reset();
    }

    static void TestRegisterChannelBinding(UGameDamageExecution* Target, const FGameplayTag& ChannelTag, const FGameplayAttribute& TargetAttribute, EGameplayModOp::Type ModOp = EGameplayModOp::Additive) {
        static_cast<FTestDamageExecutionProxy*>(Target)->RegisterChannelBinding(ChannelTag, TargetAttribute, ModOp);
    }
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDamageExecutionTest,
    "Game.Gameplay.DamageExecution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameDamageExecutionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    AActor* SourceActor = NewObject<AActor>(GetTransientPackage());
    AActor* TargetActor = NewObject<AActor>(GetTransientPackage());

    UGameAbilitySystemComponent* SourceASC = NewObject<UGameAbilitySystemComponent>(SourceActor);
    SourceASC->InitAbilityActorInfo(SourceActor, SourceActor);

    UGameAbilitySystemComponent* TargetASC = NewObject<UGameAbilitySystemComponent>(TargetActor);
    TargetASC->InitAbilityActorInfo(TargetActor, TargetActor);

    UAbilitySystemTestAttributeSet* SourceAttrSet = NewObject<UAbilitySystemTestAttributeSet>(SourceASC);
    SourceASC->AddAttributeSetSubobject(SourceAttrSet);

    UAbilitySystemTestAttributeSet* TargetAttrSet = NewObject<UAbilitySystemTestAttributeSet>(TargetASC);
    TargetASC->AddAttributeSetSubobject(TargetAttrSet);

    const FGameplayAttribute HealthAttr(FindFieldChecked<FProperty>(
        UAbilitySystemTestAttributeSet::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)
    ));
    TargetASC->SetNumericAttributeBase(HealthAttr, 100.0f);

    const FGameplayTag PhysChannelTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Physical")));
    const FGameplayTag FireChannelTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Fire")));

    UGameDamageExecution* MutableExec = GetMutableDefault<UGameDamageExecution>();

    FGameplayEffectAttributeCaptureDefinition TargetDef;
    FGameplayEffectAttributeCaptureDefinition SourceDef;
    FTestDamageExecutionProxy::TestRegisterTargetAttribute(MutableExec, HealthAttr, TargetDef, false);
    FTestDamageExecutionProxy::TestRegisterSourceAttribute(MutableExec, HealthAttr, SourceDef, true);
    TestTrue(TEXT("目标捕获属性已记录"), TargetDef.AttributeToCapture == HealthAttr);
    TestTrue(TEXT("来源捕获属性已记录"), SourceDef.AttributeToCapture == HealthAttr);

    UClass* ExecClass = UGameDamageExecution::StaticClass();
    FGameDamageChannelRegistry& Registry = FGameDamageChannelRegistry::Get();
    Registry.UnregisterAll(ExecClass);
    TestFalse(TEXT("空 ExecCalc 类注册运行期绑定失败"), UGameDamageExecution::RegisterRuntimeChannelBinding(nullptr, PhysChannelTag, HealthAttr));
    TestFalse(TEXT("无效通道标签注册运行期绑定失败"), UGameDamageExecution::RegisterRuntimeChannelBinding(ExecClass, FGameplayTag(), HealthAttr));
    TestFalse(TEXT("无效目标属性注册运行期绑定失败"), UGameDamageExecution::RegisterRuntimeChannelBinding(ExecClass, PhysChannelTag, FGameplayAttribute()));

    FTestDamageExecutionProxy::TestRegisterChannelBinding(MutableExec, FGameplayTag(), HealthAttr);
    FTestDamageExecutionProxy::TestRegisterChannelBinding(MutableExec, PhysChannelTag, FGameplayAttribute());
    FTestDamageExecutionProxy::TestRegisterChannelBinding(MutableExec, PhysChannelTag, HealthAttr);

    UGameplayEffect* Effect = NewObject<UGameplayEffect>(GetTransientPackage());
    Effect->DurationPolicy = EGameplayEffectDurationType::Instant;

    FGameplayEffectContextHandle ContextHandle(new FGameGameplayEffectContext());
    ContextHandle.AddInstigator(SourceActor, SourceActor);

    FGameplayEffectSpec Spec(Effect, ContextHandle, 1.0f);
    Spec.SetSetByCallerMagnitude(PhysChannelTag, 30.0f);
    Spec.SetSetByCallerMagnitude(FireChannelTag, 20.0f);

    FGameplayEffectCustomExecutionParameters ExecParams(Spec, TArray<FGameplayEffectExecutionScopedModifierInfo>(), TargetASC, FGameplayTagContainer(), FPredictionKey());
    FGameplayEffectCustomExecutionOutput EmptyBindingsOutput;
    Registry.UnregisterAll(ExecClass);
    const UGameDamageExecution* ExecInstance = GetDefault<UGameDamageExecution>();
    ExecInstance->Execute_Implementation(ExecParams, EmptyBindingsOutput);
    TArray<FGameplayModifierEvaluatedData> EmptyModifiers;
    EmptyBindingsOutput.GetOutputModifiers(EmptyModifiers);
    TestEqual(TEXT("无通道绑定时立即短路返回无修饰器"), EmptyModifiers.Num(), 0);

    UGameDamageExecution::RegisterRuntimeChannelBinding(ExecClass, PhysChannelTag, HealthAttr, EGameplayModOp::Additive);
    UGameDamageExecution::RegisterRuntimeChannelBinding(ExecClass, FireChannelTag, HealthAttr, EGameplayModOp::Additive);

    FGameplayEffectCustomExecutionOutput ExecOutput;
    ExecInstance->Execute_Implementation(ExecParams, ExecOutput);

    TArray<FGameplayModifierEvaluatedData> Modifiers;
    ExecOutput.GetOutputModifiers(Modifiers);
    TestEqual(TEXT("结算生成 2 个通道修改器"), Modifiers.Num(), 2);

    float EvaluatedTotal = 0.0f;
    for (const FGameplayModifierEvaluatedData& Mod : Modifiers) {
        EvaluatedTotal += Mod.Magnitude;
    }
    TestEqual(TEXT("通道合计总输出伤害为 50.0"), EvaluatedTotal, 50.0f);

    FGameGameplayEffectContext* ResultContext = FGameGameplayEffectContext::ExtractEffectContext(Spec.GetContext());
    TestNotNull(TEXT("提取上下文成功"), ResultContext);
    if (ResultContext != nullptr) {
        TestEqual(TEXT("上下文回写实际总伤害正确"), ResultContext->GetActualDamage(), 50.0f);
        TestEqual(TEXT("物理通道实际伤害回写正确"), ResultContext->GetActualChannelDamage(PhysChannelTag), 30.0f);
        TestEqual(TEXT("火焰通道实际伤害回写正确"), ResultContext->GetActualChannelDamage(FireChannelTag), 20.0f);
    }

    FGameplayEffectContextHandle PartialContextHandle(new FGameGameplayEffectContext());
    FGameplayEffectSpec PartialSpec(Effect, PartialContextHandle, 1.0f);
    PartialSpec.SetSetByCallerMagnitude(PhysChannelTag, 0.0f);

    FGameplayEffectCustomExecutionParameters PartialExecParams(PartialSpec, TArray<FGameplayEffectExecutionScopedModifierInfo>(), TargetASC, FGameplayTagContainer(), FPredictionKey());
    FGameplayEffectCustomExecutionOutput PartialExecOutput;
    ExecInstance->Execute_Implementation(PartialExecParams, PartialExecOutput);

    TArray<FGameplayModifierEvaluatedData> PartialModifiers;
    PartialExecOutput.GetOutputModifiers(PartialModifiers);
    TestEqual(TEXT("零伤害通道与未触发通道不产生输出修饰器"), PartialModifiers.Num(), 0);

    FGameGameplayEffectContext* PartialResultContext = FGameGameplayEffectContext::ExtractEffectContext(PartialSpec.GetContext());
    TestNotNull(TEXT("部分通道提取上下文有效"), PartialResultContext);
    if (PartialResultContext != nullptr) {
        TestEqual(TEXT("零伤害通道记录实际伤害为 0"), PartialResultContext->GetActualChannelDamage(PhysChannelTag), 0.0f);
        TestEqual(TEXT("部分通道总伤害为 0"), PartialResultContext->GetActualDamage(), 0.0f);
    }

    FGameplayEffectContextHandle NativeContextHandle(new FGameplayEffectContext());
    FGameplayEffectSpec NativeSpec(Effect, NativeContextHandle, 1.0f);
    NativeSpec.SetSetByCallerMagnitude(PhysChannelTag, 45.0f);

    FGameplayEffectCustomExecutionParameters NativeExecParams(NativeSpec, TArray<FGameplayEffectExecutionScopedModifierInfo>(), TargetASC, FGameplayTagContainer(), FPredictionKey());
    FGameplayEffectCustomExecutionOutput NativeExecOutput;
    ExecInstance->Execute_Implementation(NativeExecParams, NativeExecOutput);

    TArray<FGameplayModifierEvaluatedData> NativeModifiers;
    NativeExecOutput.GetOutputModifiers(NativeModifiers);
    TestEqual(TEXT("原生上下文环境输出 1 个修饰器"), NativeModifiers.Num(), 1);
    if (NativeModifiers.Num() == 1) {
        TestEqual(TEXT("原生上下文修饰器伤害正确"), NativeModifiers[0].Magnitude, 45.0f);
    }

    FTestDamageExecutionProxy::ResetCapturedAttributes(MutableExec);
    Registry.UnregisterAll(ExecClass);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
