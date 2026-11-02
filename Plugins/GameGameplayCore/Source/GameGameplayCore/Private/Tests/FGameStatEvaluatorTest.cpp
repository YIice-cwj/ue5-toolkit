#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Stat/FGameStatAcknowledgedDiff.h"
#include "Stat/FGameStatEffectiveEvaluator.h"
#include "Stat/FGameStatModifierContainer.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatEffectiveEvaluatorTest,
    "Game.GameplayCore.Stat.EffectiveEvaluator",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameStatEffectiveEvaluatorTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Evaluator.Attack")));
    FGameStatModifierContainer Modifiers;

    TestEqual(TEXT("无修正器时当前值等于基础值"),
        FGameStatEffectiveEvaluator::EvaluateCurrent(100.0f, 0.0f, AttackTag, Modifiers), 100.0f);
    TestEqual(TEXT("无修正器时上限等于基础上限"),
        FGameStatEffectiveEvaluator::EvaluateMax(100.0f, 0.0f, AttackTag, Modifiers), 100.0f);

    Modifiers.AddModifier(AttackTag, FGameStatModifier(20.0f, EGameStatModifierOp::Add));
    Modifiers.AddModifier(AttackTag, FGameStatModifier(2.0f, EGameStatModifierOp::Multiply));
    TestEqual(TEXT("加算后乘算的派生当前值"),
        FGameStatEffectiveEvaluator::EvaluateCurrent(100.0f, 0.0f, AttackTag, Modifiers), 240.0f);

    Modifiers.ClearModifiers(AttackTag);
    Modifiers.AddModifier(AttackTag, FGameStatModifier(-30.0f, EGameStatModifierOp::Add));
    TestEqual(TEXT("演算结果低于下限时钳制到下限"),
        FGameStatEffectiveEvaluator::EvaluateCurrent(60.0f, 50.0f, AttackTag, Modifiers), 50.0f);

    Modifiers.ClearModifiers(AttackTag);
    Modifiers.AddModifier(AttackTag, FGameStatModifier(50.0f, EGameStatModifierOp::Add, false, true));
    TestEqual(TEXT("仅作用于上限的修正器不改变当前值"),
        FGameStatEffectiveEvaluator::EvaluateCurrent(0.0f, 0.0f, AttackTag, Modifiers), 0.0f);
    TestEqual(TEXT("仅作用于上限的修正器提升上限"),
        FGameStatEffectiveEvaluator::EvaluateMax(100.0f, 0.0f, AttackTag, Modifiers), 150.0f);

    TestNearlyEqual(TEXT("百分比计算正确"), FGameStatEffectiveEvaluator::EvaluatePercent(50.0f, 100.0f, 0.0f), 0.5f, 0.0001f);
    TestEqual(TEXT("区间非法时百分比为零"), FGameStatEffectiveEvaluator::EvaluatePercent(50.0f, 50.0f, 50.0f), 0.0f);
    TestEqual(TEXT("超出上限时百分比钳制为一"), FGameStatEffectiveEvaluator::EvaluatePercent(150.0f, 100.0f, 0.0f), 1.0f);
    TestEqual(TEXT("低于下限时百分比钳制为零"), FGameStatEffectiveEvaluator::EvaluatePercent(-10.0f, 100.0f, 0.0f), 0.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatAcknowledgedDiffTest,
    "Game.GameplayCore.Stat.AcknowledgedDiff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameStatAcknowledgedDiffTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Diff.Health")));
    FGameStatAcknowledgedDiff Diff;

    float OldCurrent = 0.0f;
    float OldMax = 0.0f;
    TestTrue(TEXT("首次到达需要广播"), Diff.NeedsBroadcast(HealthTag, 80.0f, 100.0f, OldCurrent, OldMax));
    TestEqual(TEXT("首次到达的旧值等于本次当前值"), OldCurrent, 80.0f);
    TestEqual(TEXT("首次到达的旧上限等于本次上限"), OldMax, 100.0f);

    Diff.Record(HealthTag, 80.0f, 100.0f);
    TestFalse(TEXT("数值未变化时无需广播"), Diff.NeedsBroadcast(HealthTag, 80.0f, 100.0f, OldCurrent, OldMax));

    TestTrue(TEXT("当前值变化需要广播"), Diff.NeedsBroadcast(HealthTag, 60.0f, 100.0f, OldCurrent, OldMax));
    TestEqual(TEXT("变化的旧值等于上次已确认值"), OldCurrent, 80.0f);

    Diff.Record(HealthTag, 60.0f, 100.0f);
    TestTrue(TEXT("仅上限变化需要广播"), Diff.NeedsBroadcast(HealthTag, 60.0f, 120.0f, OldCurrent, OldMax));
    TestEqual(TEXT("仅上限变化时旧上限为已确认值"), OldMax, 100.0f);

    Diff.Record(HealthTag, 60.0f, 120.0f);
    Diff.Remove(HealthTag);
    TestTrue(TEXT("移除记录后重新视为首次到达"), Diff.NeedsBroadcast(HealthTag, 60.0f, 120.0f, OldCurrent, OldMax));
    TestEqual(TEXT("移除后旧值重新等于本次值"), OldCurrent, 60.0f);

    Diff.Record(HealthTag, 60.0f, 120.0f);
    TMap<FGameplayTag, int32> RemainingIndex;
    RemainingIndex.Add(HealthTag, 0);
    Diff.PruneMissing(RemainingIndex);
    TestFalse(TEXT("索引仍包含时不清理"), Diff.NeedsBroadcast(HealthTag, 60.0f, 120.0f, OldCurrent, OldMax));

    TMap<FGameplayTag, int32> EmptyIndex;
    Diff.PruneMissing(EmptyIndex);
    TestTrue(TEXT("索引不再包含时清理并视为首次"), Diff.NeedsBroadcast(HealthTag, 60.0f, 120.0f, OldCurrent, OldMax));

    Diff.Reset();
    TestTrue(TEXT("重置后视为首次"), Diff.NeedsBroadcast(HealthTag, 10.0f, 10.0f, OldCurrent, OldMax));

    return true;
}

#endif
