/**
 * @file UGameBaseAttributeSetTest.cpp
 * @brief 通用属性集基类（阈值单次广播、复位、元属性提取归零与本地增量落地）自动化测试套件
 */

#include "Attribute/UGameBaseAttributeSet.h"

#if WITH_AUTOMATION_TESTS

#include <limits>

#include "Ability/UGameAbilitySystemComponent.h"
#include "AbilitySystemTestAttributeSet.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBaseAttributeSetTest,
    "Game.Gameplay.AttributeSet",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 BroadcastThresholdOnce 幂等与非法标签拦截、ResetThreshold、MoveMetaAttribute 与 ApplyAttributeToLocal 正负向及非有限浮点防御
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBaseAttributeSetTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(GetTransientPackage());
    UAbilitySystemTestAttributeSet* AttrSet = NewObject<UAbilitySystemTestAttributeSet>(ASC);
    ASC->AddAttributeSetSubobject(AttrSet);

    const FGameplayAttribute HealthAttr(FindFieldChecked<FProperty>(
        UAbilitySystemTestAttributeSet::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)
    ));
    const FGameplayTag ThresholdTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Threshold.LowHealth")));

    TestFalse(TEXT("无效阈值标签触发返回 false"), UGameBaseAttributeSet::BroadcastThresholdOnce(ASC, FGameplayTag()));
    TestEqual(TEXT("无效阈值标签不污染 ASC 标签集"), ASC->GetOwnedGameplayTags().Num(), 0);
    UGameBaseAttributeSet::ResetThreshold(ASC, FGameplayTag());

    TestTrue(TEXT("首次达阈授予松标签并返回 true"), UGameBaseAttributeSet::BroadcastThresholdOnce(ASC, ThresholdTag));
    TestTrue(TEXT("ASC 持有临界标签"), ASC->HasMatchingGameplayTag(ThresholdTag));
    TestFalse(TEXT("再次达阈幂等短路返回 false"), UGameBaseAttributeSet::BroadcastThresholdOnce(ASC, ThresholdTag));

    UGameBaseAttributeSet::ResetThreshold(ASC, ThresholdTag);
    TestFalse(TEXT("重置后 ASC 不再持有临界标签"), ASC->HasMatchingGameplayTag(ThresholdTag));
    TestTrue(TEXT("重置后可再次达阈返回 true"), UGameBaseAttributeSet::BroadcastThresholdOnce(ASC, ThresholdTag));

    TestFalse(TEXT("空 ASC 达阈判定返回 false"), UGameBaseAttributeSet::BroadcastThresholdOnce(nullptr, ThresholdTag));
    UGameBaseAttributeSet::ResetThreshold(nullptr, ThresholdTag);

    ASC->SetNumericAttributeBase(HealthAttr, 100.0f);
    UGameBaseAttributeSet::ApplyAttributeToLocal(ASC, HealthAttr, -35.0f);
    TestEqual(TEXT("ApplyAttributeToLocal 扣减增量落地正确"), ASC->GetNumericAttributeBase(HealthAttr), 65.0f);

    UGameBaseAttributeSet::ApplyAttributeToLocal(ASC, HealthAttr, 20.0f);
    TestEqual(TEXT("ApplyAttributeToLocal 回复增量落地正确"), ASC->GetNumericAttributeBase(HealthAttr), 85.0f);

    UGameBaseAttributeSet::ApplyAttributeToLocal(ASC, HealthAttr, std::numeric_limits<float>::quiet_NaN());
    TestEqual(TEXT("NaN 增量被拦截不污染属性"), ASC->GetNumericAttributeBase(HealthAttr), 85.0f);

    UGameBaseAttributeSet::ApplyAttributeToLocal(ASC, HealthAttr, std::numeric_limits<float>::infinity());
    TestEqual(TEXT("正无穷增量被拦截不污染属性"), ASC->GetNumericAttributeBase(HealthAttr), 85.0f);

    UGameBaseAttributeSet::ApplyAttributeToLocal(ASC, HealthAttr, -std::numeric_limits<float>::infinity());
    TestEqual(TEXT("负无穷增量被拦截不污染属性"), ASC->GetNumericAttributeBase(HealthAttr), 85.0f);

    UGameBaseAttributeSet::ApplyAttributeToLocal(nullptr, HealthAttr, 10.0f);
    UGameBaseAttributeSet::ApplyAttributeToLocal(ASC, FGameplayAttribute(), 10.0f);
    TestEqual(TEXT("无效属性与空 ASC 不影响已有属性"), ASC->GetNumericAttributeBase(HealthAttr), 85.0f);

    FGameplayAttributeData MetaData;
    MetaData.SetBaseValue(85.0f);
    MetaData.SetCurrentValue(85.0f);
    TestEqual(TEXT("MoveMetaAttribute 提取当前数值"), UGameBaseAttributeSet::MoveMetaAttribute(MetaData), 85.0f);
    TestEqual(TEXT("MoveMetaAttribute 提取后归零 Base"), MetaData.GetBaseValue(), 0.0f);
    TestEqual(TEXT("MoveMetaAttribute 提取后归零 Current"), MetaData.GetCurrentValue(), 0.0f);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
