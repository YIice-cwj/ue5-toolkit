/**
 * @file UGameplayAttributeValueMappingTest.cpp
 * @brief 属性表现契约映射工具自动化测试套件
 */

#include "Attribute/UGameplayAttributeValueMapping.h"

#if WITH_AUTOMATION_TESTS

#include "Ability/UGameAbilitySystemComponent.h"
#include "AbilitySystemTestAttributeSet.h"
#include "Attribute/UGameBaseAttributeSet.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAttributeValueMappingTest,
    "Game.Gameplay.ValueMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证工厂函数元数据、属性变动通知、主动查询广播、空 ASC 安全短路及解绑清理
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameAttributeValueMappingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const UFunction* FactoryFunction = UGameplayAttributeValueMapping::StaticClass()->FindFunctionByName(TEXT("CreateAttributeValueMapping"));
    TestNotNull(TEXT("属性映射工厂反射信息可用"), FactoryFunction);
    if (FactoryFunction == nullptr) {
        return false;
    }
    TestEqual(
        TEXT("工厂默认不在对象返回前广播"),
        FactoryFunction->GetMetaData(TEXT("CPP_Default_bBroadcastImmediately")),
        FString(TEXT("false"))
    );

    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(GetTransientPackage());
    UAbilitySystemTestAttributeSet* AttrSet = NewObject<UAbilitySystemTestAttributeSet>(ASC);
    ASC->AddAttributeSetSubobject(AttrSet);

    const FGameplayAttribute HealthAttr(FindFieldChecked<FProperty>(UAbilitySystemTestAttributeSet::StaticClass(), GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)));
    ASC->SetNumericAttributeBase(HealthAttr, 150.0f);

    float ObservedNew = 0.0f;
    float ObservedOld = 0.0f;
    int32 BroadcastCount = 0;

    UGameplayAttributeValueMapping* Mapping = UGameplayAttributeValueMapping::CreateAttributeValueMapping(ASC, HealthAttr);

    Mapping->OnValueChangedNative.AddLambda([&](float NewVal, float OldVal) {
        ObservedNew = NewVal;
        ObservedOld = OldVal;
        ++BroadcastCount;
    });

    Mapping->QueryAndBroadcast();
    TestEqual(TEXT("立即广播获得正确初值"), ObservedNew, 150.0f);
    TestEqual(TEXT("获取当前值正确"), Mapping->GetCurrentValue(), 150.0f);
    TestEqual(TEXT("订阅后仅主动查询广播一次"), BroadcastCount, 1);

    ASC->SetNumericAttributeBase(HealthAttr, 200.0f);
    TestEqual(TEXT("属性变动触发映射广播"), ObservedNew, 200.0f);

    UGameBaseAttributeSet::ApplyAttributeToLocal(ASC, HealthAttr, -50.0f);
    TestEqual(TEXT("ApplyAttributeToLocal 落地增量正确"), ASC->GetNumericAttributeBase(HealthAttr), 150.0f);

    Mapping->Unbind();
    TestEqual(TEXT("解绑后数值查询返回 0"), Mapping->GetCurrentValue(), 0.0f);
    TestFalse(TEXT("解绑后 IsBound 返回 false"), Mapping->IsBound());
    TestFalse(TEXT("解绑后 BoundAttribute 无效"), Mapping->GetBoundAttribute().IsValid());

    TestNull(TEXT("空 ASC 创建映射返回 nullptr"), UGameplayAttributeValueMapping::CreateAttributeValueMapping(nullptr, nullptr, HealthAttr));

    UGameplayAttributeValueMapping* MappingWithoutOuter = UGameplayAttributeValueMapping::CreateAttributeValueMapping(nullptr, ASC, HealthAttr, false);
    TestNotNull(TEXT("空 Outer 创建映射成功且 Outer 自动降级为 ASC"), MappingWithoutOuter);
    TestTrue(TEXT("映射状态处于绑定中"), MappingWithoutOuter->IsBound());
    TestEqual(TEXT("绑定的属性对齐"), MappingWithoutOuter->GetBoundAttribute(), HealthAttr);
    TestEqual(TEXT("绑定的属性数值对齐"), MappingWithoutOuter->GetCurrentValue(), 150.0f);

    MappingWithoutOuter->Bind(nullptr, HealthAttr);
    TestTrue(TEXT("空 ASC 重新绑定安全短路保持原绑定状态"), MappingWithoutOuter->IsBound());
    TestEqual(TEXT("空 ASC 重新绑定数值保持"), MappingWithoutOuter->GetCurrentValue(), 150.0f);

    UGameplayAttributeValueMapping* ImmediateMapping = UGameplayAttributeValueMapping::CreateAttributeValueMapping(GetTransientPackage(), ASC, HealthAttr, true);
    TestNotNull(TEXT("开启即时广播创建映射成功"), ImmediateMapping);
    TestEqual(TEXT("即时广播初始化获取当前值正确"), ImmediateMapping->GetCurrentValue(), 150.0f);
    ImmediateMapping->Unbind();

    return true;
}

#endif  // WITH_AUTOMATION_TESTS

