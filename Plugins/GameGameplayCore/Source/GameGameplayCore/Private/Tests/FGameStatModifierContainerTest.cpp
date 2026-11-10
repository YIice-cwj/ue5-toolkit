/**
 * @file FGameStatModifierContainerTest.cpp
 * @brief FGameStatModifierContainer 修正器叠加与演算自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Stat/FGameStatModifierContainer.h"
#include "Stat/UGameStatComponent.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatModifierTest,
    "Game.GameplayCore.Stat.ModifierStack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证修正器叠加、加算乘算覆写优先级、句柄精确移除与派生值实时计算
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatModifierTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Modifier.Attack")));

    Comp->RegisterStat(AttackTag, 100.0f, 100.0f, 0.0f);
    TestEqual(TEXT("无修正器时派生值等于基础值"), Comp->GetStatEffectiveCurrent(AttackTag), 100.0f);

    const uint32 AddHandle = Comp->AddStatModifier(AttackTag, FGameStatModifier(20.0f, EGameStatModifierOp::Add)).GetValue();
    const uint32 MultiplyHandle = Comp->AddStatModifier(AttackTag, FGameStatModifier(2.0f, EGameStatModifierOp::Multiply)).GetValue();
    TestEqual(TEXT("加算后乘算的派生值正确"), Comp->GetStatEffectiveCurrent(AttackTag), 240.0f);
    TestEqual(TEXT("基础值不被修正器改写"), Comp->GetStatCurrent(AttackTag), 100.0f);

    TestTrue(TEXT("按句柄移除加算修正器"), Comp->RemoveStatModifier(AttackTag, AddHandle).IsSuccess());
    TestEqual(TEXT("移除加算后仅剩乘算"), Comp->GetStatEffectiveCurrent(AttackTag), 200.0f);

    const uint32 OverrideHandle = Comp->AddStatModifier(AttackTag, FGameStatModifier(7.0f, EGameStatModifierOp::Override)).GetValue();
    TestEqual(TEXT("覆写修正器最终生效"), Comp->GetStatEffectiveCurrent(AttackTag), 7.0f);

    TestEqual(TEXT("清空全部修正器"), Comp->ClearStatModifiers(AttackTag), 2);
    TestEqual(TEXT("清空后派生值回落到基础值"), Comp->GetStatEffectiveCurrent(AttackTag), 100.0f);
    TestTrue(TEXT("已移除句柄不可重复移除"), Comp->RemoveStatModifier(AttackTag, MultiplyHandle).IsFailure());
    TestTrue(TEXT("已移除覆写句柄不可重复移除"), Comp->RemoveStatModifier(AttackTag, OverrideHandle).IsFailure());

    int32 ModifierBroadcastCount = 0;
    float LastBroadcastNew = 0.0f;
    float LastBroadcastMax = 0.0f;
    Comp->SubscribeStatChanged(
        AttackTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda(
            [&ModifierBroadcastCount, &LastBroadcastNew, &LastBroadcastMax](const FGameplayTag&, float, float NewValue, float MaxValue) {
                ++ModifierBroadcastCount;
                LastBroadcastNew = NewValue;
                LastBroadcastMax = MaxValue;
            }
        )
    );
    TestEqual(TEXT("初始订阅回放一次"), ModifierBroadcastCount, 1);

    const uint32 NotifyHandle = Comp->AddStatModifier(AttackTag, FGameStatModifier(25.0f, EGameStatModifierOp::Add)).GetValue();
    TestEqual(TEXT("新增修正器触发订阅广播"), ModifierBroadcastCount, 2);
    TestEqual(TEXT("广播新有效值正确"), LastBroadcastNew, 125.0f);
    TestTrue(TEXT("移除修正器成功"), Comp->RemoveStatModifier(AttackTag, NotifyHandle).IsSuccess());
    TestEqual(TEXT("移除修正器触发订阅广播"), ModifierBroadcastCount, 3);
    TestEqual(TEXT("移除后广播有效值回落"), LastBroadcastNew, 100.0f);

    const FGameplayTag HealthMaxOnlyTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Modifier.HealthMaxOnly")));
    Comp->RegisterStat(HealthMaxOnlyTag, 0.0f, 100.0f, 0.0f);
    Comp->AddStatModifier(HealthMaxOnlyTag, FGameStatModifier(50.0f, EGameStatModifierOp::Add, false, true));
    TestEqual(TEXT("仅作用于上限的修正器不抬高零血当前值"), Comp->GetStatEffectiveCurrent(HealthMaxOnlyTag), 0.0f);
    TestEqual(TEXT("仅作用于上限的修正器正确提升有效上限"), Comp->GetStatEffectiveMax(HealthMaxOnlyTag), 150.0f);

    return true;
}

#endif
