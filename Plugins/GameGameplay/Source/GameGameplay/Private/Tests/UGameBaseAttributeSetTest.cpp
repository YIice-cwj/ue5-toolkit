/**
 * @file UGameBaseAttributeSetTest.cpp
 * @brief 通用属性集基类（钳制、阈值单次广播、本地修改与元属性移动）自动化测试套件
 */

#include "Attribute/UGameBaseAttributeSet.h"

#if WITH_AUTOMATION_TESTS

#include "Ability/UGameAbilitySystemComponent.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBaseAttributeSetTest,
    "Game.Gameplay.AttributeSet",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 ClampAttribute、BroadcastThresholdOnce 幂等性、ResetThreshold 及 ApplyAttributeToLocal
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBaseAttributeSetTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TestEqual(TEXT("ClampAttribute 上限钳制"), UGameBaseAttributeSet::ClampAttribute(120.0f, 0.0f, 100.0f), 100.0f);
    TestEqual(TEXT("ClampAttribute 下限钳制"), UGameBaseAttributeSet::ClampAttribute(-20.0f, 0.0f, 100.0f), 0.0f);
    TestEqual(TEXT("ClampAttribute 范围内直通"), UGameBaseAttributeSet::ClampAttribute(50.0f, 0.0f, 100.0f), 50.0f);

    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(GetTransientPackage());
    const FGameplayTag ThresholdTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Threshold.LowHealth")));

    TestTrue(TEXT("首次达阈授予松标签并返回 true"), UGameBaseAttributeSet::BroadcastThresholdOnce(ASC, ThresholdTag));
    TestTrue(TEXT("ASC 持有临界标签"), ASC->HasMatchingGameplayTag(ThresholdTag));
    TestFalse(TEXT("再次达阈幂等短路返回 false"), UGameBaseAttributeSet::BroadcastThresholdOnce(ASC, ThresholdTag));

    UGameBaseAttributeSet::ResetThreshold(ASC, ThresholdTag);
    TestFalse(TEXT("重置后 ASC 不再持有临界标签"), ASC->HasMatchingGameplayTag(ThresholdTag));
    TestTrue(TEXT("重置后可再次达阈返回 true"), UGameBaseAttributeSet::BroadcastThresholdOnce(ASC, ThresholdTag));

    TestFalse(TEXT("空 ASC 达阈判定返回 false"), UGameBaseAttributeSet::BroadcastThresholdOnce(nullptr, ThresholdTag));
    UGameBaseAttributeSet::ResetThreshold(nullptr, ThresholdTag);
    UGameBaseAttributeSet::ApplyAttributeToLocal(nullptr, FGameplayAttribute(), 10.0f);

    FGameplayAttributeData MetaData;
    MetaData.SetBaseValue(85.0f);
    MetaData.SetCurrentValue(85.0f);
    TestEqual(TEXT("MoveMetaAttribute 提取当前数值"), UGameBaseAttributeSet::MoveMetaAttribute(MetaData), 85.0f);
    TestEqual(TEXT("MoveMetaAttribute 提取后归零 Base"), MetaData.GetBaseValue(), 0.0f);
    TestEqual(TEXT("MoveMetaAttribute 提取后归零 Current"), MetaData.GetCurrentValue(), 0.0f);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS

