/**
 * @file FGameBuffModifierBridgeTest.cpp
 * @brief FGameBuffModifierBridge 修正器桥接容器、原子刷新零抖动防误杀与挂接回滚自动化测试
 */

#include "CoreMinimal.h"
#include "Buff/FGameBuffInstance.h"
#include "Buff/FGameBuffModifierBridge.h"
#include "Buff/UGameBuffComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Stat/UGameStatComponent.h"
#include "Stat/UGameVitalComponent.h"
#include "Tests/GameGameplayCoreTestUtils.h"

#include <limits>

#if WITH_AUTOMATION_TESTS

using Game::GameplayCore::Test::FScopedTestHostActor;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffModifierBridgeTest,
    "Game.GameplayCore.Buff.ModifierBridge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameBuffModifierBridge 在空规格、非法条目过滤、单层与多层加算/乘算/覆写及 ClearAll 时的挂接与回滚
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

    FGameStatModifierSpec InvalidTagModSpec;
    InvalidTagModSpec.Modifier = FGameStatModifier(999.0f, EGameStatModifierOp::Add);
    AddSpec.Modifiers.Add(InvalidTagModSpec);

    FGameStatModifierSpec NaNModSpec;
    NaNModSpec.StatTag = AttackTag;
    NaNModSpec.Modifier = FGameStatModifier(std::numeric_limits<float>::quiet_NaN(), EGameStatModifierOp::Add);
    AddSpec.Modifiers.Add(NaNModSpec);

    FGameBuffInstance SingleStackInstance;
    SingleStackInstance.InstanceId = 6;
    SingleStackInstance.Spec = AddSpec;
    SingleStackInstance.Stacks = 1;
    Bridge.Register(SingleStackInstance.InstanceId, SingleStackInstance, *StatComponent);
    TestEqual(TEXT("单层加算修正器生效且自动忽略非法与 NaN 条目"), StatComponent->GetStatEffectiveCurrent(AttackTag), 120.0f);
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
    MultiplierInstance.Spec.Modifiers.SetNum(1);
    MultiplierInstance.Spec.Modifiers[0].Modifier = FGameStatModifier(1.5f, EGameStatModifierOp::Multiply);
    Bridge.Register(MultiplierInstance.InstanceId, MultiplierInstance, *StatComponent);
    TestNearlyEqual(TEXT("乘算修正器按层数缩放"), StatComponent->GetStatEffectiveCurrent(AttackTag), 200.0f, 0.0001f);
    Bridge.Unregister(MultiplierInstance.InstanceId, StatComponent);

    FGameBuffInstance OverrideInstance = ThreeStackInstance;
    OverrideInstance.InstanceId = 9;
    OverrideInstance.Stacks = 4;
    OverrideInstance.Spec.Modifiers.SetNum(1);
    OverrideInstance.Spec.Modifiers[0].Modifier = FGameStatModifier(50.0f, EGameStatModifierOp::Override);
    Bridge.Register(OverrideInstance.InstanceId, OverrideInstance, *StatComponent);
    TestEqual(TEXT("多层覆写修正器保持原值不缩放"), StatComponent->GetStatEffectiveCurrent(AttackTag), 50.0f);
    Bridge.Unregister(OverrideInstance.InstanceId, StatComponent);

    Bridge.Register(ThreeStackInstance.InstanceId, ThreeStackInstance, *StatComponent);
    Bridge.Register(MultiplierInstance.InstanceId, MultiplierInstance, *StatComponent);
    TestTrue(TEXT("多实例同时注册生效"), StatComponent->GetStatEffectiveCurrent(AttackTag) > 100.0f);

    Bridge.ClearAll(*StatComponent);
    TestEqual(TEXT("ClearAll 批量清空全部修正器并回滚至基础值"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);

    const FGameplayTag DefenseTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Bridge.Defense")));
    StatComponent->RegisterStat(DefenseTag, 50.0f, 500.0f, 0.0f);

    FGameBuffInstance DualModInstance;
    DualModInstance.InstanceId = 10;
    DualModInstance.Stacks = 1;
    DualModInstance.Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Bridge.DualMod")));
    FGameStatModifierSpec AtkPart;
    AtkPart.StatTag = AttackTag;
    AtkPart.Modifier = FGameStatModifier(10.0f, EGameStatModifierOp::Add);
    FGameStatModifierSpec DefPart;
    DefPart.StatTag = DefenseTag;
    DefPart.Modifier = FGameStatModifier(5.0f, EGameStatModifierOp::Add);
    DualModInstance.Spec.Modifiers.Add(AtkPart);
    DualModInstance.Spec.Modifiers.Add(DefPart);

    Bridge.Register(DualModInstance.InstanceId, DualModInstance, *StatComponent);
    TestEqual(TEXT("双修正器 Attack 生效"), StatComponent->GetStatEffectiveCurrent(AttackTag), 110.0f);
    TestEqual(TEXT("双修正器 Defense 生效"), StatComponent->GetStatEffectiveCurrent(DefenseTag), 55.0f);

    StatComponent->ClearStatModifiers(DefenseTag);
    int32 AtkFallbackBroadcasts = 0;
    StatComponent->SubscribeStatChanged(
        AttackTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda([&AtkFallbackBroadcasts](const FGameplayTag&, float, float, float) {
            ++AtkFallbackBroadcasts;
        })
    );
    AtkFallbackBroadcasts = 0;

    DualModInstance.Stacks = 2;
    Bridge.Register(DualModInstance.InstanceId, DualModInstance, *StatComponent);
    TestEqual(TEXT("部分原地更新失败回退重建仍合并为单次 Attack 原子广播"), AtkFallbackBroadcasts, 1);
    TestEqual(TEXT("回退重建后 Attack 缩放值正确"), StatComponent->GetStatEffectiveCurrent(AttackTag), 120.0f);
    TestEqual(TEXT("回退重建后 Defense 缩放值恢复正确"), StatComponent->GetStatEffectiveCurrent(DefenseTag), 60.0f);

    Bridge.ClearAllSilent(*StatComponent);
    TestEqual(TEXT("ClearAllSilent 静默回滚 Attack 不触发广播"), AtkFallbackBroadcasts, 1);
    TestEqual(TEXT("ClearAllSilent 回滚后 Attack 恢复基础值"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);
    TestEqual(TEXT("ClearAllSilent 回滚后 Defense 恢复基础值"), StatComponent->GetStatEffectiveCurrent(DefenseTag), 50.0f);

    FGameBuffInstance OverflowInstance;
    OverflowInstance.InstanceId = 11;
    OverflowInstance.Stacks = 10;
    OverflowInstance.Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Bridge.ScaleOverflow")));
    FGameStatModifierSpec HugeSpec;
    HugeSpec.StatTag = AttackTag;
    HugeSpec.Modifier = FGameStatModifier(1.0e38f, EGameStatModifierOp::Add);
    OverflowInstance.Spec.Modifiers.Add(HugeSpec);
    Bridge.Register(OverflowInstance.InstanceId, OverflowInstance, *StatComponent);
    TestEqual(TEXT("高叠层缩放上溢为 Inf 的修正器被过滤，不毒化属性"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffAtomicRefreshNoDeathTest,
    "Game.GameplayCore.Buff.AtomicRefreshNoDeath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证靠生命 Buff 维持存活的实体在刷新时长、叠层及部分扣层时，原子更新零中间态回落且绝不误触发死亡
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffAtomicRefreshNoDeathTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* StatComp = NewObject<UGameStatComponent>(GetTransientPackage());
    UGameVitalComponent* VitalComp = NewObject<UGameVitalComponent>(GetTransientPackage());
    UGameBuffComponent* BuffComp = NewObject<UGameBuffComponent>(GetTransientPackage());

    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.AtomicRefresh.Health")));
    const FGameplayTag ShieldBuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.AtomicRefresh.LifeGuard")));

    StatComp->RegisterStat(HealthTag, 0.0f, 100.0f, 0.0f);
    VitalComp->BindStatComponent(StatComp);
    VitalComp->SetPrimaryVitalTag(HealthTag);
    BuffComp->BindStatComponent(StatComp);

    FGameBuffSpec LifeGuardSpec;
    LifeGuardSpec.BuffTag = ShieldBuffTag;
    LifeGuardSpec.Duration = 10.0f;
    LifeGuardSpec.MaxStacks = 2;
    LifeGuardSpec.StackPolicy = EGameBuffStackPolicy::RefreshDuration;
    FGameStatModifierSpec HealthBonus;
    HealthBonus.StatTag = HealthTag;
    HealthBonus.Modifier = FGameStatModifier(50.0f, EGameStatModifierOp::Add);
    LifeGuardSpec.Modifiers.Add(HealthBonus);

    TestTrue(TEXT("施加保命 Buff 成功"), BuffComp->ApplyBuff(LifeGuardSpec).IsSuccess());
    TestEqual(TEXT("有效生命抬升至 50"), StatComp->GetStatEffectiveCurrent(HealthTag), 50.0f);
    TestTrue(TEXT("实体进入存活态"), VitalComp->IsVitalAlive());

    int32 StatBroadcastCount = 0;
    float LastStatOld = 0.0f;
    float LastStatNew = 0.0f;
    int32 VitalBroadcastCount = 0;
    StatComp->OnStatChangedNative.AddLambda([&StatBroadcastCount, &LastStatOld, &LastStatNew](const FGameplayTag&, float OldVal, float NewVal, float) {
        ++StatBroadcastCount;
        LastStatOld = OldVal;
        LastStatNew = NewVal;
    });
    VitalComp->OnVitalStateChangedNative.AddLambda([&VitalBroadcastCount](bool, const FGameplayTag&) {
        ++VitalBroadcastCount;
    });

    TestTrue(TEXT("叠加至第 2 层成功"), BuffComp->ApplyBuff(LifeGuardSpec).IsSuccess());
    TestEqual(TEXT("叠层仅触发单次原子递增广播"), StatBroadcastCount, 1);
    TestEqual(TEXT("叠层广播旧值为 50 而非中间回落 0"), LastStatOld, 50.0f);
    TestEqual(TEXT("叠层广播新值为 100"), LastStatNew, 100.0f);
    TestEqual(TEXT("叠层全程零生死状态误跳变"), VitalBroadcastCount, 0);
    TestTrue(TEXT("叠层后保持存活"), VitalComp->IsVitalAlive());

    TestTrue(TEXT("满层刷新持续时间成功"), BuffComp->ApplyBuff(LifeGuardSpec).IsSuccess());
    TestEqual(TEXT("满层刷新零属性广播抖动"), StatBroadcastCount, 1);
    TestEqual(TEXT("满层刷新零生死误杀"), VitalBroadcastCount, 0);

    TestTrue(TEXT("部分扣减 1 层成功"), BuffComp->RemoveBuffStacks(ShieldBuffTag, 1).IsSuccess());
    TestEqual(TEXT("部分扣层仅触发单次原子回落广播"), StatBroadcastCount, 2);
    TestEqual(TEXT("部分扣层广播旧值为 100"), LastStatOld, 100.0f);
    TestEqual(TEXT("部分扣层广播新值为 50"), LastStatNew, 50.0f);
    TestEqual(TEXT("部分扣层全程零生死误杀"), VitalBroadcastCount, 0);
    TestTrue(TEXT("剩余 1 层仍保持存活"), VitalComp->IsVitalAlive());

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

    TestTrue(TEXT("无数值组件时仍可施加 Buff"), BuffComponent->ApplyBuff(Spec).IsSuccess());
    TestEqual(TEXT("未绑定时基础值保持不变"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);

    BuffComponent->BindStatComponent(StatComponent);
    TestEqual(TEXT("晚绑定后既有 Buff 修正器生效"), StatComponent->GetStatEffectiveCurrent(AttackTag), 110.0f);

    TestTrue(TEXT("清理测试 Buff"), BuffComponent->RemoveBuff(Spec.BuffTag).IsSuccess());
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
    const FGameStatModifierHandle OtherModifierHandle =
        StatB->AddStatModifier(AttackTag, FGameStatModifier(5.0f, EGameStatModifierOp::Add)).GetValue();
    TestEqual(TEXT("重绑前 StatA 生效"), StatA->GetStatEffectiveCurrent(AttackTag), 110.0f);
    TestEqual(TEXT("重绑前 StatB 仅有其他修正器"), StatB->GetStatEffectiveCurrent(AttackTag), 105.0f);

    BuffComponent->BindStatComponent(StatB);
    TestEqual(TEXT("重绑后 StatA 完整回滚"), StatA->GetStatEffectiveCurrent(AttackTag), 100.0f);
    TestEqual(TEXT("重绑后 StatB 叠加 Buff 与其他修正器"), StatB->GetStatEffectiveCurrent(AttackTag), 115.0f);

    TestTrue(TEXT("重绑后移除 Buff 成功"), BuffComponent->RemoveBuff(Spec.BuffTag).IsSuccess());
    TestEqual(TEXT("移除后 StatA 保持基础值"), StatA->GetStatEffectiveCurrent(AttackTag), 100.0f);
    TestEqual(TEXT("移除后 StatB 保留其他修正器"), StatB->GetStatEffectiveCurrent(AttackTag), 105.0f);
    TestTrue(TEXT("StatB 其他修正器句柄仍有效"), StatB->RemoveStatModifier(AttackTag, OtherModifierHandle).IsSuccess());
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
    FScopedTestHostActor Host;
    TestTrue(TEXT("测试宿主 Actor 有效"), Host.IsValidHost());
    if (!Host.IsValidHost()) {
        return false;
    }

    UGameStatComponent* StatComponent = NewObject<UGameStatComponent>(Host.Get());
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(Host.Get());
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

    return true;
}

#endif
