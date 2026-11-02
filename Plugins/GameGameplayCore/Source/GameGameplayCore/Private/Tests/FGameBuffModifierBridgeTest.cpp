#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Buff/FGameBuffModifierBridge.h"
#include "Stat/UGameStatComponent.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffModifierBridgeTest,
    "Game.GameplayCore.Buff.ModifierBridge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameBuffModifierBridgeTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Bridge.Attack")));
    UGameStatComponent* StatComponent = NewObject<UGameStatComponent>();
    StatComponent->RegisterStat(AttackTag, 100.0f, 1000.0f, 0.0f);

    FGameBuffSpec Spec;
    Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Bridge.AttackBoost")));
    FGameStatModifierSpec AddSpec;
    AddSpec.StatTag = AttackTag;
    AddSpec.Modifier = FGameStatModifier(20.0f, EGameStatModifierOp::Add);
    Spec.Modifiers.Add(AddSpec);

    FGameBuffInstance Instance;
    Instance.InstanceId = 7;
    Instance.Spec = Spec;
    Instance.Stacks = 3;

    FGameBuffModifierBridge Bridge;
    Bridge.Register(Instance.InstanceId, Instance, *StatComponent);
    TestEqual(TEXT("三层加算修正器按层数缩放到派生值"), StatComponent->GetStatEffectiveCurrent(AttackTag), 160.0f);

    Bridge.Unregister(Instance.InstanceId, StatComponent);
    TestEqual(TEXT("注销实例后派生值回落到基础值"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);

    Bridge.Unregister(Instance.InstanceId, StatComponent);
    TestEqual(TEXT("重复注销同一实例无副作用"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);

    FGameBuffInstance MultiplierInstance = Instance;
    MultiplierInstance.InstanceId = 8;
    MultiplierInstance.Stacks = 2;
    MultiplierInstance.Spec.Modifiers[0].Modifier = FGameStatModifier(1.5f, EGameStatModifierOp::Multiply);
    Bridge.Register(MultiplierInstance.InstanceId, MultiplierInstance, *StatComponent);
    TestNearlyEqual(TEXT("乘算修正器按层数缩放"), StatComponent->GetStatEffectiveCurrent(AttackTag), 200.0f, 0.0001f);

    Bridge.Unregister(MultiplierInstance.InstanceId, nullptr);
    TestNearlyEqual(TEXT("数值组件为空时仅清理映射不回滚"), StatComponent->GetStatEffectiveCurrent(AttackTag), 200.0f, 0.0001f);

    Bridge.Reset();
    TestNearlyEqual(TEXT("重置映射不影响已挂接修正器"), StatComponent->GetStatEffectiveCurrent(AttackTag), 200.0f, 0.0001f);

    return true;
}

#endif
