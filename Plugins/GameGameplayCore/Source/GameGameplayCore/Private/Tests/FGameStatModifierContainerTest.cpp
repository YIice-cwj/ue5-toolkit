/**
 * @file FGameStatModifierContainerTest.cpp
 * @brief FGameStatModifierContainer 修正器叠加、原地更新、代际防 ABA 与批处理演算自动化测试
 */

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Stat/FGameStatModifierContainer.h"
#include "Stat/UGameStatComponent.h"

#include <limits>

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatModifierContainerDirectTest,
    "Game.GameplayCore.Stat.ModifierContainerDirect",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 直接验证 FGameStatModifierContainer 的加乘覆写顺序、下限钳制、双轴演算、原地更新与 Reset 代际防 ABA
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatModifierContainerDirectTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.ContainerDirect.Attack")));
    const FGameplayTag EmptyTag;
    FGameStatModifierContainer Container;

    TestFalse(TEXT("非法标签添加修正器返回无效句柄"), Container.AddModifier(EmptyTag, FGameStatModifier(10.0f)).IsValid());
    TestFalse(TEXT("NaN 修正值被拒绝"), Container.AddModifier(AttackTag, FGameStatModifier(std::numeric_limits<float>::quiet_NaN())).IsValid());
    TestFalse(TEXT("Inf 修正值被拒绝"), Container.AddModifier(AttackTag, FGameStatModifier(std::numeric_limits<float>::infinity())).IsValid());

    TestEqual(TEXT("无修正器时 EvaluateClamped 返回基础值"), Container.EvaluateClamped(AttackTag, 100.0f, 0.0f, false), 100.0f);
    TestEqual(TEXT("无修正器但基础值低于下限时钳制到下限"), Container.EvaluateClamped(AttackTag, -5.0f, 10.0f, false), 10.0f);

    const FGameStatModifierHandle AddHandle = Container.AddModifier(AttackTag, FGameStatModifier(20.0f, EGameStatModifierOp::Add, true, false));
    const FGameStatModifierHandle MaxMultHandle = Container.AddModifier(AttackTag, FGameStatModifier(1.5f, EGameStatModifierOp::Multiply, false, true));
    const FGameStatModifierHandle NoTargetHandle = Container.AddModifier(AttackTag, FGameStatModifier(999.0f, EGameStatModifierOp::Add, false, false));
    TestTrue(TEXT("合法修正器句柄均有效"), AddHandle.IsValid() && MaxMultHandle.IsValid() && NoTargetHandle.IsValid());

    const FGameStatValue BaseVal{80.0f, 100.0f, 0.0f};
    const FGameStatEffectivePair Pair = Container.EvaluateEffectivePair(AttackTag, BaseVal);
    TestEqual(TEXT("双轴演算 Current 仅受 Add 影响"), Pair.Current, 100.0f);
    TestEqual(TEXT("双轴演算 Max 仅受 Multiply 影响"), Pair.Max, 150.0f);

    TestTrue(TEXT("原地 UpdateModifier 成功"), Container.UpdateModifier(AttackTag, AddHandle, FGameStatModifier(35.0f, EGameStatModifierOp::Add, true, false)));
    TestFalse(TEXT("原地 UpdateModifier 拒绝 NaN"), Container.UpdateModifier(AttackTag, AddHandle, FGameStatModifier(std::numeric_limits<float>::quiet_NaN())));
    TestEqual(TEXT("原地更新后有效当前值即时生效"), Container.EvaluateClamped(AttackTag, 80.0f, 0.0f, false), 115.0f);

    const FGameStatModifierHandle FirstOverride = Container.AddModifier(AttackTag, FGameStatModifier(40.0f, EGameStatModifierOp::Override));
    const FGameStatModifierHandle SecondOverride = Container.AddModifier(AttackTag, FGameStatModifier(75.0f, EGameStatModifierOp::Override));
    TestEqual(TEXT("多个 Override 后注册者优先"), Container.EvaluateClamped(AttackTag, 80.0f, 0.0f, false), 75.0f);

    TestTrue(TEXT("移除靠后 Override 后回退到靠前 Override"), Container.RemoveModifier(AttackTag, SecondOverride));
    TestEqual(TEXT("保持插入顺序生效"), Container.EvaluateClamped(AttackTag, 80.0f, 0.0f, false), 40.0f);
    TestTrue(TEXT("移除首位 Override 成功"), Container.RemoveModifier(AttackTag, FirstOverride));

    Container.ClearModifiers(AttackTag);
    const FGameStatModifierHandle NegativeAdd = Container.AddModifier(AttackTag, FGameStatModifier(-200.0f, EGameStatModifierOp::Add));
    TestEqual(TEXT("负向修正低于下限时钳制到下限"), Container.EvaluateClamped(AttackTag, 60.0f, 50.0f, false), 50.0f);
    TestTrue(TEXT("句柄有效"), NegativeAdd.IsValid());

    Container.Reset();
    const FGameStatModifierHandle PostResetHandle = Container.AddModifier(AttackTag, FGameStatModifier(15.0f, EGameStatModifierOp::Add));
    TestNotEqual(TEXT("Reset 后代际号递增，新旧句柄不相等"), PostResetHandle.PackUInt64(), NegativeAdd.PackUInt64());
    TestFalse(TEXT("Reset 前签发的过期句柄绝不能误删新修正器 (ABA 防护)"), Container.RemoveModifier(AttackTag, NegativeAdd));
    TestEqual(TEXT("新修正器保持完好"), Container.EvaluateClamped(AttackTag, 100.0f, 0.0f, false), 115.0f);

    TestNearlyEqual(TEXT("百分比计算正确"), FGameStatValue::EvaluatePercent(50.0f, 100.0f, 0.0f), 0.5f, 0.0001f);
    TestEqual(TEXT("区间非法时百分比为零"), FGameStatValue::EvaluatePercent(50.0f, 50.0f, 50.0f), 0.0f);
    TestEqual(TEXT("超出上限时百分比钳制为一"), FGameStatValue::EvaluatePercent(150.0f, 100.0f, 0.0f), 1.0f);
    TestEqual(TEXT("低于下限时百分比钳制为零"), FGameStatValue::EvaluatePercent(-10.0f, 100.0f, 0.0f), 0.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatModifierTest,
    "Game.GameplayCore.Stat.ModifierStack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证修正器叠加、加算乘算覆写优先级、代际句柄精确移除、原地更新与批量合并广播
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatModifierTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Modifier.Attack")));

    Comp->RegisterStat(AttackTag, 100.0f, 100.0f, 0.0f);
    TestEqual(TEXT("无修正器时派生值等于基础值"), Comp->GetStatEffectiveCurrent(AttackTag), 100.0f);

    const FGameStatModifierHandle AddHandle = Comp->AddStatModifier(AttackTag, FGameStatModifier(20.0f, EGameStatModifierOp::Add)).GetValue();
    const FGameStatModifierHandle MultiplyHandle = Comp->AddStatModifier(AttackTag, FGameStatModifier(2.0f, EGameStatModifierOp::Multiply)).GetValue();
    TestEqual(TEXT("加算后乘算的派生值正确"), Comp->GetStatEffectiveCurrent(AttackTag), 240.0f);
    TestEqual(TEXT("基础值不被修正器改写"), Comp->GetStatCurrent(AttackTag), 100.0f);

    TestTrue(TEXT("按句柄移除加算修正器"), Comp->RemoveStatModifier(AttackTag, AddHandle).IsSuccess());
    TestEqual(TEXT("移除加算后仅剩乘算"), Comp->GetStatEffectiveCurrent(AttackTag), 200.0f);

    const FGameStatModifierHandle OverrideHandle = Comp->AddStatModifier(AttackTag, FGameStatModifier(7.0f, EGameStatModifierOp::Override)).GetValue();
    TestEqual(TEXT("覆写修正器最终生效"), Comp->GetStatEffectiveCurrent(AttackTag), 7.0f);

    TestEqual(TEXT("清空全部修正器"), Comp->ClearStatModifiers(AttackTag), 2);
    TestEqual(TEXT("清空后派生值回落到基础值"), Comp->GetStatEffectiveCurrent(AttackTag), 100.0f);

    const TGameStatusResult<void> DupRemoveRes = Comp->RemoveStatModifier(AttackTag, MultiplyHandle);
    TestTrue(TEXT("已移除句柄不可重复移除"), DupRemoveRes.IsFailure());
    TestEqual(TEXT("错误归属 Gameplay 模块"), DupRemoveRes.GetError().ModuleId, Game::ModuleId::Gameplay);
    TestEqual(TEXT("错误码为 ModifierHandleNotFound"), DupRemoveRes.GetError().ErrorCode, Game::GameplayCoreErrorCodes::ModifierHandleNotFound);
    TestTrue(TEXT("已移除覆写句柄不可重复移除"), Comp->RemoveStatModifier(AttackTag, OverrideHandle).IsFailure());

    int32 ModifierBroadcastCount = 0;
    float LastBroadcastOld = 0.0f;
    float LastBroadcastNew = 0.0f;
    Comp->SubscribeStatChanged(
        AttackTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda(
            [&ModifierBroadcastCount, &LastBroadcastOld, &LastBroadcastNew](const FGameplayTag&, float OldValue, float NewValue, float) {
                ++ModifierBroadcastCount;
                LastBroadcastOld = OldValue;
                LastBroadcastNew = NewValue;
            }
        )
    );
    TestEqual(TEXT("初始订阅回放一次"), ModifierBroadcastCount, 1);

    const FGameStatModifierHandle NotifyHandle = Comp->AddStatModifier(AttackTag, FGameStatModifier(25.0f, EGameStatModifierOp::Add)).GetValue();
    TestEqual(TEXT("新增修正器触发订阅广播"), ModifierBroadcastCount, 2);
    TestEqual(TEXT("广播新有效值正确"), LastBroadcastNew, 125.0f);

    TestTrue(TEXT("原地更新修正器成功"), Comp->UpdateStatModifier(AttackTag, NotifyHandle, FGameStatModifier(40.0f, EGameStatModifierOp::Add)).IsSuccess());
    TestEqual(TEXT("原地更新仅触发一次增量广播"), ModifierBroadcastCount, 3);
    TestEqual(TEXT("原地更新广播旧值为 125"), LastBroadcastOld, 125.0f);
    TestEqual(TEXT("原地更新广播新值为 140"), LastBroadcastNew, 140.0f);

    {
        UGameStatComponent::FScopedModifierBatch Batch(*Comp);
        Comp->RemoveStatModifier(AttackTag, NotifyHandle);
        Comp->AddStatModifier(AttackTag, FGameStatModifier(40.0f, EGameStatModifierOp::Add));
    }
    TestEqual(TEXT("批处理内先删后加等价净变零，抑制虚假中间广播"), ModifierBroadcastCount, 3);

    {
        UGameStatComponent::FScopedModifierBatch Batch(*Comp);
        Comp->ClearStatModifiers(AttackTag);
        Comp->AddStatModifier(AttackTag, FGameStatModifier(60.0f, EGameStatModifierOp::Add));
    }
    TestEqual(TEXT("批处理内多次变更合并为单次终态广播"), ModifierBroadcastCount, 4);
    TestEqual(TEXT("合并广播旧值为批处理前快照 140"), LastBroadcastOld, 140.0f);
    TestEqual(TEXT("合并广播新值为批处理后终态 160"), LastBroadcastNew, 160.0f);

    {
        UGameStatComponent::FScopedModifierBatch Batch(*Comp);
        TestTrue(TEXT("批处理内移除无效句柄返回失败"), Comp->RemoveStatModifier(AttackTag, MultiplyHandle).IsFailure());
        TestTrue(TEXT("批处理内直接修改基础值触发一次立即广播"), Comp->ModifyStat(AttackTag, -10.0f));
    }
    TestEqual(TEXT("失败的修正器操作不污染批处理基线，退出批处理不重复广播"), ModifierBroadcastCount, 5);

    const FGameplayTag HealthMaxOnlyTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Modifier.HealthMaxOnly")));
    Comp->RegisterStat(HealthMaxOnlyTag, 0.0f, 100.0f, 0.0f);
    Comp->AddStatModifier(HealthMaxOnlyTag, FGameStatModifier(50.0f, EGameStatModifierOp::Add, false, true));
    TestEqual(TEXT("仅作用于上限的修正器不抬高零血当前值"), Comp->GetStatEffectiveCurrent(HealthMaxOnlyTag), 0.0f);
    TestEqual(TEXT("仅作用于上限的修正器正确提升有效上限"), Comp->GetStatEffectiveMax(HealthMaxOnlyTag), 150.0f);

    return true;
}

#endif
