/**
 * @file UGameDamageExecutionTest.cpp
 * @brief 泛型伤害结算 ExecCalc 基类与自定义子类（修饰管线、治疗负值、Override(0)、事件 ID 保护与防死锁）自动化测试套件
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
#include "Misc/ScopeExit.h"
#include "Tests/UGameDamageTestExecution.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace {

class FTestDamageExecutionProxy : public UGameDamageExecution {
public:
    static void TestRegisterTargetAttribute(
        UGameDamageExecution* Target,
        const FGameplayAttribute& InAttribute,
        FGameplayEffectAttributeCaptureDefinition& OutDefinition,
        bool bSnapshot
    ) {
        static_cast<FTestDamageExecutionProxy*>(Target)->RegisterTargetAttribute(InAttribute, OutDefinition, bSnapshot);
    }

    static void TestRegisterSourceAttribute(
        UGameDamageExecution* Target,
        const FGameplayAttribute& InAttribute,
        FGameplayEffectAttributeCaptureDefinition& OutDefinition,
        bool bSnapshot
    ) {
        static_cast<FTestDamageExecutionProxy*>(Target)->RegisterSourceAttribute(InAttribute, OutDefinition, bSnapshot);
    }

    static void ResetCapturedAttributes(UGameDamageExecution* Target) {
        static_cast<FTestDamageExecutionProxy*>(Target)->RelevantAttributesToCapture.Reset();
    }

    static void TestRegisterChannelBinding(
        UGameDamageExecution* Target,
        const FGameplayTag& ChannelTag,
        const FGameplayAttribute& TargetAttribute,
        EGameplayModOp::Type ModOp = EGameplayModOp::Additive
    ) {
        static_cast<FTestDamageExecutionProxy*>(Target)->RegisterChannelBinding(ChannelTag, TargetAttribute, ModOp);
    }
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDamageExecutionTest,
    "Game.Gameplay.DamageExecution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证属性捕获注册、多通道结算、Override(0)、治疗负通道、事件 ID 保护、GE 标签聚合、反馈钩子及钩子重入防死锁
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
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
    const FGameplayTag HealChannelTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Heal")));
    const FGameplayTag OverrideChannelTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.OverrideZero")));
    const FGameplayTag ReentrantTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.ReentrantHook")));
    const FGameplayTag SourceActorTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Test.ExecSourceOwned")));
    const FGameplayTag SpecDynamicTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Effect.Test.SpecDynamicTag")));
    const FGameplayTag TargetActorTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Test.ExecTargetOwned")));
    const FGameplayTag PostCritTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.ExecPostCrit")));
    const FGameplayTag PostHitStopTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Dynamic.Test.ExecPostHitStop")));

    UGameDamageExecution* MutableExec = GetMutableDefault<UGameDamageExecution>();
    UClass* ExecClass = UGameDamageExecution::StaticClass();
    UClass* CustomExecClass = UGameDamageTestExecution::StaticClass();
    FGameDamageChannelRegistry& Registry = FGameDamageChannelRegistry::Get();

    ON_SCOPE_EXIT {
        FTestDamageExecutionProxy::ResetCapturedAttributes(MutableExec);
        Registry.UnregisterAll(ExecClass);
        Registry.UnregisterAll(CustomExecClass);
    };

    FGameplayEffectAttributeCaptureDefinition TargetDef;
    FGameplayEffectAttributeCaptureDefinition SourceDef;
    FTestDamageExecutionProxy::TestRegisterTargetAttribute(MutableExec, HealthAttr, TargetDef, false);
    FTestDamageExecutionProxy::TestRegisterSourceAttribute(MutableExec, HealthAttr, SourceDef, true);
    TestTrue(TEXT("目标捕获属性已记录"), TargetDef.AttributeToCapture == HealthAttr);
    TestTrue(TEXT("来源捕获属性已记录"), SourceDef.AttributeToCapture == HealthAttr);
    TestTrue(TEXT("来源捕获属性快照标记一致"), SourceDef.bSnapshot);

    Registry.UnregisterAll(ExecClass);
    Registry.UnregisterAll(CustomExecClass);

    FTestDamageExecutionProxy::TestRegisterChannelBinding(MutableExec, FGameplayTag(), HealthAttr);
    FTestDamageExecutionProxy::TestRegisterChannelBinding(MutableExec, PhysChannelTag, FGameplayAttribute());
    FTestDamageExecutionProxy::TestRegisterChannelBinding(MutableExec, PhysChannelTag, HealthAttr);
    TestEqual(TEXT("基类保护方法 RegisterChannelBinding 仅注册有效条目"), Registry.Num(ExecClass), 1);

    UGameplayEffect* Effect = NewObject<UGameplayEffect>(GetTransientPackage());
    Effect->DurationPolicy = EGameplayEffectDurationType::Instant;

    FGameGameplayEffectContext* PresetIdContext = new FGameGameplayEffectContext();
    PresetIdContext->SetFeedbackEventId(777);
    FGameplayEffectContextHandle ContextHandle(PresetIdContext);
    ContextHandle.AddInstigator(SourceActor, SourceActor);

    FGameplayEffectSpec Spec(Effect, ContextHandle, 1.0f);
    Spec.SetSetByCallerMagnitude(PhysChannelTag, 30.0f);
    Spec.SetSetByCallerMagnitude(FireChannelTag, 20.0f);

    FGameplayEffectCustomExecutionParameters ExecParams(
        Spec,
        TArray<FGameplayEffectExecutionScopedModifierInfo>(),
        TargetASC,
        FGameplayTagContainer(),
        FPredictionKey()
    );
    FGameplayEffectCustomExecutionOutput EmptyBindingsOutput;
    Registry.UnregisterAll(ExecClass);
    const UGameDamageExecution* ExecInstance = GetDefault<UGameDamageExecution>();
    ExecInstance->Execute_Implementation(ExecParams, EmptyBindingsOutput);
    TArray<FGameplayModifierEvaluatedData> EmptyModifiers;
    EmptyBindingsOutput.GetOutputModifiers(EmptyModifiers);
    TestEqual(TEXT("无通道绑定时立即短路返回无修饰器"), EmptyModifiers.Num(), 0);

    Registry.Register(ExecClass, FDamageChannelBinding(PhysChannelTag, HealthAttr, EGameplayModOp::Additive));
    Registry.Register(ExecClass, FDamageChannelBinding(FireChannelTag, HealthAttr, EGameplayModOp::Additive));

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
        TestEqual(TEXT("无预测 Key 执行时绝不覆写已预置的 FeedbackEventId"), ResultContext->GetFeedbackEventId(), static_cast<uint16>(777));
        TestEqual(TEXT("上下文回写实际总伤害正确"), ResultContext->GetActualDamage(), 50.0f);
        TestEqual(TEXT("物理通道实际伤害回写正确"), ResultContext->GetActualChannelDamage(PhysChannelTag), 30.0f);
        TestEqual(TEXT("火焰通道实际伤害回写正确"), ResultContext->GetActualChannelDamage(FireChannelTag), 20.0f);
    }

    FGameplayEffectContextHandle PartialContextHandle(new FGameGameplayEffectContext());
    FGameplayEffectSpec PartialSpec(Effect, PartialContextHandle, 1.0f);
    PartialSpec.SetSetByCallerMagnitude(PhysChannelTag, 0.0f);

    FGameplayEffectCustomExecutionParameters PartialExecParams(
        PartialSpec,
        TArray<FGameplayEffectExecutionScopedModifierInfo>(),
        TargetASC,
        FGameplayTagContainer(),
        FPredictionKey()
    );
    FGameplayEffectCustomExecutionOutput PartialExecOutput;
    ExecInstance->Execute_Implementation(PartialExecParams, PartialExecOutput);

    TArray<FGameplayModifierEvaluatedData> PartialModifiers;
    PartialExecOutput.GetOutputModifiers(PartialModifiers);
    TestEqual(TEXT("Additive 零伤害通道与未触发通道不产生输出修饰器"), PartialModifiers.Num(), 0);

    FGameGameplayEffectContext* PartialResultContext = FGameGameplayEffectContext::ExtractEffectContext(PartialSpec.GetContext());
    TestNotNull(TEXT("部分通道提取上下文有效"), PartialResultContext);
    if (PartialResultContext != nullptr) {
        TestEqual(TEXT("零伤害通道记录实际伤害为 0"), PartialResultContext->GetActualChannelDamage(PhysChannelTag), 0.0f);
        TestEqual(TEXT("部分通道总伤害为 0"), PartialResultContext->GetActualDamage(), 0.0f);
    }

    FGameplayEffectContextHandle NativeContextHandle(new FGameplayEffectContext());
    FGameplayEffectSpec NativeSpec(Effect, NativeContextHandle, 1.0f);
    NativeSpec.SetSetByCallerMagnitude(PhysChannelTag, 45.0f);

    FGameplayEffectCustomExecutionParameters NativeExecParams(
        NativeSpec,
        TArray<FGameplayEffectExecutionScopedModifierInfo>(),
        TargetASC,
        FGameplayTagContainer(),
        FPredictionKey()
    );
    FGameplayEffectCustomExecutionOutput NativeExecOutput;
    ExecInstance->Execute_Implementation(NativeExecParams, NativeExecOutput);

    TArray<FGameplayModifierEvaluatedData> NativeModifiers;
    NativeExecOutput.GetOutputModifiers(NativeModifiers);
    TestEqual(TEXT("原生上下文环境输出 1 个修饰器"), NativeModifiers.Num(), 1);
    if (NativeModifiers.Num() == 1) {
        TestEqual(TEXT("原生上下文修饰器伤害正确"), NativeModifiers[0].Magnitude, 45.0f);
    }

    Registry.UnregisterAll(ExecClass);
    Registry.Register(CustomExecClass, FDamageChannelBinding(HealChannelTag, HealthAttr, EGameplayModOp::Additive));
    Registry.Register(CustomExecClass, FDamageChannelBinding(OverrideChannelTag, HealthAttr, EGameplayModOp::Override));

    SourceASC->AddLooseGameplayTag(SourceActorTag);
    TargetASC->AddLooseGameplayTag(TargetActorTag);

    UGameDamageTestExecution* CustomExec = NewObject<UGameDamageTestExecution>(GetTransientPackage());
    CustomExec->TestModifier = FDamageModifier(1.5f, 10.0f);
    CustomExec->PostFeedbackTag = PostCritTag;
    CustomExec->PostDynamicValueTag = PostHitStopTag;
    CustomExec->PostDynamicValue = 0.15f;
    CustomExec->ReentrantRegistrationBinding = FDamageChannelBinding(ReentrantTag, HealthAttr, EGameplayModOp::Additive);

    FGameplayEffectContextHandle CustomCtxHandle(new FGameGameplayEffectContext());
    CustomCtxHandle.AddInstigator(SourceActor, SourceActor);
    FGameplayEffectSpec CustomSpec(Effect, CustomCtxHandle, 1.0f);
    CustomSpec.AddDynamicAssetTag(SpecDynamicTag);
    CustomSpec.CapturedSourceTags.GetSpecTags().AddTag(SpecDynamicTag);
    CustomSpec.SetSetByCallerMagnitude(HealChannelTag, -30.0f);

    FPredictionKey ValidPredictionKey;
    ValidPredictionKey.Current = 42;
    FGameplayEffectCustomExecutionParameters CustomParams(
        CustomSpec,
        TArray<FGameplayEffectExecutionScopedModifierInfo>(),
        TargetASC,
        FGameplayTagContainer(),
        ValidPredictionKey
    );
    FGameplayEffectCustomExecutionOutput CustomOutput;
    CustomExec->Execute_Implementation(CustomParams, CustomOutput);

    TArray<FGameplayModifierEvaluatedData> CustomMods;
    CustomOutput.GetOutputModifiers(CustomMods);
    TestEqual(TEXT("自定义子类治疗负通道产出 1 个修饰器"), CustomMods.Num(), 1);
    if (CustomMods.Num() == 1) {
        TestEqual(TEXT("治疗负通道经先加后乘修饰结果为 (-30 + 10) * 1.5 = -30"), CustomMods[0].Magnitude, -30.0f);
    }
    TestTrue(TEXT("ComputeChannelModifier 观测到 Spec 动态标签"), CustomExec->LastObservedSourceTags.HasTag(SpecDynamicTag));
    TestTrue(TEXT("ComputeChannelModifier 观测到目标 ASC 持有标签"), CustomExec->LastObservedTargetTags.HasTag(TargetActorTag));
    TestEqual(TEXT("OnPostExecuteChannels 被调用 1 次"), CustomExec->PostExecuteCallCount, 1);
    TestEqual(TEXT("OnPostExecuteChannels 内重入注册新通道无死锁且生效"), Registry.Num(CustomExecClass), 3);

    FGameGameplayEffectContext* CustomResultCtx = FGameGameplayEffectContext::ExtractEffectContext(CustomSpec.GetContext());
    TestNotNull(TEXT("自定义子类提取上下文成功"), CustomResultCtx);
    if (CustomResultCtx != nullptr) {
        TestEqual(TEXT("有效预测 Key 写入 FeedbackEventId 正确"), CustomResultCtx->GetFeedbackEventId(), static_cast<uint16>(42));
        TestTrue(TEXT("OnPostExecuteChannels 注入暴击反馈标签"), CustomResultCtx->HasFeedbackTag(PostCritTag));
        TestEqual(TEXT("OnPostExecuteChannels 注入卡肉动态值"), CustomResultCtx->GetDynamicValue(PostHitStopTag), 0.15f);
    }

    CustomExec->TestModifier = FDamageModifier::MakeOverride(0.0f);
    CustomExec->ReentrantRegistrationBinding = FDamageChannelBinding();
    FGameplayEffectContextHandle OverrideCtxHandle(new FGameGameplayEffectContext());
    FGameplayEffectSpec OverrideSpec(Effect, OverrideCtxHandle, 1.0f);
    OverrideSpec.SetSetByCallerMagnitude(OverrideChannelTag, 100.0f);
    FGameplayEffectCustomExecutionParameters OverrideParams(
        OverrideSpec,
        TArray<FGameplayEffectExecutionScopedModifierInfo>(),
        TargetASC,
        FGameplayTagContainer(),
        FPredictionKey()
    );
    FGameplayEffectCustomExecutionOutput OverrideOutput;
    CustomExec->Execute_Implementation(OverrideParams, OverrideOutput);

    TArray<FGameplayModifierEvaluatedData> OverrideMods;
    OverrideOutput.GetOutputModifiers(OverrideMods);
    TestEqual(TEXT("ModOp == Override 且 FinalDamage == 0.0f 必须输出覆写修饰器不被吞掉"), OverrideMods.Num(), 1);
    if (OverrideMods.Num() == 1) {
        TestTrue(TEXT("输出修饰器算子为 Override"), OverrideMods[0].ModifierOp == EGameplayModOp::Override);
        TestEqual(TEXT("输出修饰器覆写值为 0.0f"), OverrideMods[0].Magnitude, 0.0f);
    }

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
