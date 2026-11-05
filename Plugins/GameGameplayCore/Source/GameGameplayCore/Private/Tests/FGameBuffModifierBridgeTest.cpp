/**
 * @file FGameBuffModifierBridgeTest.cpp
 * @brief FGameBuffModifierBridge 修正器桥接容器与挂接回滚自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Buff/FGameBuffModifierBridge.h"
#include "Buff/UGameBuffComponent.h"
#include "Stat/UGameStatComponent.h"

#if WITH_AUTOMATION_TESTS

namespace {

UWorld* GetBridgeTestWorld() {
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World() != nullptr) {
            return Context.World();
        }
    }
    return nullptr;
}

AActor* SpawnBridgeTestHost(UWorld* InWorld) {
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return InWorld->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffModifierBridgeTest,
    "Game.GameplayCore.Buff.ModifierBridge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameBuffModifierBridge 在空规格、单层与多层加算、乘算、覆写以及批量清空时的正确挂接与回滚
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffModifierBridgeTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Bridge.Attack")));
    UGameStatComponent* StatComponent = NewObject<UGameStatComponent>();
    StatComponent->RegisterStat(AttackTag, 100.0f, 1000.0f, 0.0f);

    FGameBuffModifierBridge Bridge;

    FGameBuffInstance EmptyInstance;
    EmptyInstance.InstanceId = 5;
    Bridge.Register(EmptyInstance.InstanceId, EmptyInstance, *StatComponent);
    TestEqual(TEXT("空修正器规格注册不改变有效数值"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);

    FGameBuffSpec AddSpec;
    AddSpec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Bridge.AddBoost")));
    FGameStatModifierSpec AddModSpec;
    AddModSpec.StatTag = AttackTag;
    AddModSpec.Modifier = FGameStatModifier(20.0f, EGameStatModifierOp::Add);
    AddSpec.Modifiers.Add(AddModSpec);

    FGameBuffInstance SingleStackInstance;
    SingleStackInstance.InstanceId = 6;
    SingleStackInstance.Spec = AddSpec;
    SingleStackInstance.Stacks = 1;
    Bridge.Register(SingleStackInstance.InstanceId, SingleStackInstance, *StatComponent);
    TestEqual(TEXT("单层加算修正器生效"), StatComponent->GetStatEffectiveCurrent(AttackTag), 120.0f);
    Bridge.Register(SingleStackInstance.InstanceId, SingleStackInstance, *StatComponent);
    TestEqual(TEXT("重复调用 Register 具备自愈幂等性不累加句柄"), StatComponent->GetStatEffectiveCurrent(AttackTag), 120.0f);
    Bridge.Unregister(SingleStackInstance.InstanceId, StatComponent);
    TestEqual(TEXT("注销单层修正器回退基础值"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);

    FGameBuffInstance ThreeStackInstance;
    ThreeStackInstance.InstanceId = 7;
    ThreeStackInstance.Spec = AddSpec;
    ThreeStackInstance.Stacks = 3;
    Bridge.Register(ThreeStackInstance.InstanceId, ThreeStackInstance, *StatComponent);
    TestEqual(TEXT("三层加算修正器按层数缩放到派生值"), StatComponent->GetStatEffectiveCurrent(AttackTag), 160.0f);

    Bridge.Unregister(ThreeStackInstance.InstanceId, StatComponent);
    TestEqual(TEXT("注销实例后派生值回落到基础值"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);

    Bridge.Unregister(ThreeStackInstance.InstanceId, StatComponent);
    TestEqual(TEXT("重复注销同一实例无副作用"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);

    FGameBuffInstance MultiplierInstance = ThreeStackInstance;
    MultiplierInstance.InstanceId = 8;
    MultiplierInstance.Stacks = 2;
    MultiplierInstance.Spec.Modifiers[0].Modifier = FGameStatModifier(1.5f, EGameStatModifierOp::Multiply);
    Bridge.Register(MultiplierInstance.InstanceId, MultiplierInstance, *StatComponent);
    TestNearlyEqual(TEXT("乘算修正器按层数缩放"), StatComponent->GetStatEffectiveCurrent(AttackTag), 200.0f, 0.0001f);
    Bridge.Unregister(MultiplierInstance.InstanceId, StatComponent);

    FGameBuffInstance OverrideInstance = ThreeStackInstance;
    OverrideInstance.InstanceId = 9;
    OverrideInstance.Stacks = 4;
    OverrideInstance.Spec.Modifiers[0].Modifier = FGameStatModifier(50.0f, EGameStatModifierOp::Override);
    Bridge.Register(OverrideInstance.InstanceId, OverrideInstance, *StatComponent);
    TestEqual(TEXT("多层覆写修正器保持原值不缩放"), StatComponent->GetStatEffectiveCurrent(AttackTag), 50.0f);
    Bridge.Unregister(OverrideInstance.InstanceId, StatComponent);

    Bridge.Register(ThreeStackInstance.InstanceId, ThreeStackInstance, *StatComponent);
    Bridge.Register(MultiplierInstance.InstanceId, MultiplierInstance, *StatComponent);
    TestTrue(TEXT("多实例同时注册生效"), StatComponent->GetStatEffectiveCurrent(AttackTag) > 100.0f);

    Bridge.ClearAll(*StatComponent);
    TestEqual(TEXT("ClearAll 批量清空全部修正器并回滚至基础值"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffModifierLateBindingTest,
    "Game.GameplayCore.Buff.ModifierLateBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证无初始数值组件时施加 Buff，晚绑定数值组件后历史修正器无损重新挂接
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffModifierLateBindingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* StatComponent = NewObject<UGameStatComponent>(GetTransientPackage());
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(GetTransientPackage());
    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Buff.LateBinding")));
    StatComponent->RegisterStat(AttackTag, 100.0f, 1000.0f, 0.0f);

    FGameBuffSpec Spec;
    Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.LateBinding.Test")));
    FGameStatModifierSpec ModifierSpec;
    ModifierSpec.StatTag = AttackTag;
    ModifierSpec.Modifier = FGameStatModifier(10.0f, EGameStatModifierOp::Add);
    Spec.Modifiers.Add(ModifierSpec);

    TestNotEqual(TEXT("无数值组件时仍可施加 Buff"), BuffComponent->ApplyBuff(Spec), 0);
    TestEqual(TEXT("未绑定时基础值保持不变"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);

    BuffComponent->BindStatComponent(StatComponent);
    TestEqual(TEXT("晚绑定后既有 Buff 修正器生效"), StatComponent->GetStatEffectiveCurrent(AttackTag), 110.0f);

    TestTrue(TEXT("清理测试 Buff"), BuffComponent->RemoveBuff(Spec.BuffTag));
    TestEqual(TEXT("移除后修正器回滚"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffModifierRebindingTest,
    "Game.GameplayCore.Buff.ModifierRebinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证动态切换关联数值组件时，旧数值组件安全回滚且新数值组件无缝挂接修正器
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffModifierRebindingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* StatA = NewObject<UGameStatComponent>(GetTransientPackage());
    UGameStatComponent* StatB = NewObject<UGameStatComponent>(GetTransientPackage());
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(GetTransientPackage());
    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Buff.Rebinding")));
    StatA->RegisterStat(AttackTag, 100.0f, 1000.0f, 0.0f);
    StatB->RegisterStat(AttackTag, 100.0f, 1000.0f, 0.0f);

    FGameBuffSpec Spec;
    Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Rebinding.Test")));
    FGameStatModifierSpec ModifierSpec;
    ModifierSpec.StatTag = AttackTag;
    ModifierSpec.Modifier = FGameStatModifier(10.0f, EGameStatModifierOp::Add);
    Spec.Modifiers.Add(ModifierSpec);

    BuffComponent->BindStatComponent(StatA);
    BuffComponent->ApplyBuff(Spec);
    const uint32 OtherModifierHandle = StatB->AddStatModifier(AttackTag, FGameStatModifier(5.0f, EGameStatModifierOp::Add));
    TestEqual(TEXT("重绑前 StatA 生效"), StatA->GetStatEffectiveCurrent(AttackTag), 110.0f);
    TestEqual(TEXT("重绑前 StatB 仅有其他修正器"), StatB->GetStatEffectiveCurrent(AttackTag), 105.0f);

    BuffComponent->BindStatComponent(StatB);
    TestEqual(TEXT("重绑后 StatA 完整回滚"), StatA->GetStatEffectiveCurrent(AttackTag), 100.0f);
    TestEqual(TEXT("重绑后 StatB 叠加 Buff 与其他修正器"), StatB->GetStatEffectiveCurrent(AttackTag), 115.0f);

    TestTrue(TEXT("重绑后移除 Buff 成功"), BuffComponent->RemoveBuff(Spec.BuffTag));
    TestEqual(TEXT("移除后 StatA 保持基础值"), StatA->GetStatEffectiveCurrent(AttackTag), 100.0f);
    TestEqual(TEXT("移除后 StatB 保留其他修正器"), StatB->GetStatEffectiveCurrent(AttackTag), 105.0f);
    TestTrue(TEXT("StatB 其他修正器句柄仍有效"), StatB->RemoveStatModifier(AttackTag, OtherModifierHandle));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffModifierDestroyRollbackTest,
    "Game.GameplayCore.Buff.ModifierDestroyRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Buff 组件销毁或 EndPlay 时精确回滚所有注入到数值组件的属性修正器
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffModifierDestroyRollbackTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetBridgeTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        return false;
    }

    AActor* HostActor = SpawnBridgeTestHost(World);
    UGameStatComponent* StatComponent = NewObject<UGameStatComponent>(HostActor);
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(HostActor);
    StatComponent->RegisterComponent();
    BuffComponent->RegisterComponent();
    BuffComponent->BeginPlay();
    BuffComponent->BindStatComponent(StatComponent);

    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Buff.DestroyRollback")));
    StatComponent->RegisterStat(AttackTag, 100.0f, 1000.0f, 0.0f);
    FGameBuffSpec Spec;
    Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.DestroyRollback.Test")));
    FGameStatModifierSpec ModifierSpec;
    ModifierSpec.StatTag = AttackTag;
    ModifierSpec.Modifier = FGameStatModifier(10.0f, EGameStatModifierOp::Add);
    Spec.Modifiers.Add(ModifierSpec);

    BuffComponent->ApplyBuff(Spec);
    TestEqual(TEXT("销毁前修正器生效"), StatComponent->GetStatEffectiveCurrent(AttackTag), 110.0f);
    BuffComponent->EndPlay(EEndPlayReason::Destroyed);
    TestEqual(TEXT("Buff 组件销毁后 Stat 上的修正器回滚"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);
    BuffComponent->DestroyComponent();

    HostActor->Destroy();
    return true;
}

#endif
