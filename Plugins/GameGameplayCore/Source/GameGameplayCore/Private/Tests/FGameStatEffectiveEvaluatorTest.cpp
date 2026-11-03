/**
 * @file FGameStatEffectiveEvaluatorTest.cpp
 * @brief FGameStatEffectiveEvaluator 纯函数有效值演算自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Stat/FGameStatEffectiveEvaluator.h"
#include "Stat/FGameStatModifierContainer.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatEffectiveEvaluatorTest,
    "Game.GameplayCore.Stat.EffectiveEvaluator",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameStatEffectiveEvaluator 纯函数有效值与上限计算、钳制及百分比推导
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
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

#endif
