/**
 * @file UGameplayAttributeValueMappingTest.cpp
 * @brief 属性表现契约映射工具（双轨广播、跨 ASC 切换、GC 失效安全与对象池复用）自动化测试套件
 */

#include "Attribute/UGameplayAttributeValueMapping.h"

#if WITH_AUTOMATION_TESTS

#include "Ability/UGameAbilitySystemComponent.h"
#include "AbilitySystemTestAttributeSet.h"
#include "Core/IGamePoolable.h"
#include "Misc/AutomationTest.h"
#include "Tests/UGameAttributeMappingTestReceiver.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAttributeValueMappingTest,
    "Game.Gameplay.ValueMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证工厂创建、蓝图/原生双轨通知、跨源重绑、ASC GC 失效后 IsBound/QueryAndBroadcast 安全性及对象池复用
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameAttributeValueMappingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TStrongObjectPtr<UGameAbilitySystemComponent> ASC1(NewObject<UGameAbilitySystemComponent>(GetTransientPackage()));
    UAbilitySystemTestAttributeSet* AttrSet1 = NewObject<UAbilitySystemTestAttributeSet>(ASC1.Get());
    ASC1->AddAttributeSetSubobject(AttrSet1);

    TStrongObjectPtr<UGameAbilitySystemComponent> ASC2(NewObject<UGameAbilitySystemComponent>(GetTransientPackage()));
    UAbilitySystemTestAttributeSet* AttrSet2 = NewObject<UAbilitySystemTestAttributeSet>(ASC2.Get());
    ASC2->AddAttributeSetSubobject(AttrSet2);

    const FGameplayAttribute HealthAttr(FindFieldChecked<FProperty>(
        UAbilitySystemTestAttributeSet::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)
    ));
    ASC1->SetNumericAttributeBase(HealthAttr, 150.0f);
    ASC2->SetNumericAttributeBase(HealthAttr, 320.0f);

    float ObservedNew = 0.0f;
    float ObservedOld = 0.0f;
    int32 NativeBroadcastCount = 0;

    TStrongObjectPtr<UGameAttributeMappingTestReceiver> DynamicReceiver(NewObject<UGameAttributeMappingTestReceiver>(GetTransientPackage()));
    TStrongObjectPtr<UGameplayAttributeValueMapping> Mapping(
        UGameplayAttributeValueMapping::CreateAttributeValueMapping(GetTransientPackage(), ASC1.Get(), HealthAttr)
    );
    TestNotNull(TEXT("工厂创建映射成功"), Mapping.Get());
    if (!Mapping.IsValid()) {
        return false;
    }

    Mapping->OnValueChangedNative.AddLambda([&](float NewVal, float OldVal) {
        ObservedNew = NewVal;
        ObservedOld = OldVal;
        ++NativeBroadcastCount;
    });
    Mapping->OnValueChanged.AddDynamic(DynamicReceiver.Get(), &UGameAttributeMappingTestReceiver::HandleMappedValueChanged);

    Mapping->QueryAndBroadcast();
    TestEqual(TEXT("主动查询广播获得原生初值"), ObservedNew, 150.0f);
    TestEqual(TEXT("主动查询广播同步送达蓝图动态多播"), DynamicReceiver->LastNewValue, 150.0f);
    TestEqual(TEXT("动态多播接收次数为 1"), DynamicReceiver->CallCount, 1);
    TestEqual(TEXT("获取当前值正确"), Mapping->GetCurrentValue(), 150.0f);
    TestEqual(TEXT("订阅后仅主动查询广播一次"), NativeBroadcastCount, 1);

    ASC1->SetNumericAttributeBase(HealthAttr, 200.0f);
    TestEqual(TEXT("属性变动触发原生映射广播新值"), ObservedNew, 200.0f);
    TestEqual(TEXT("属性变动触发原生映射广播旧值"), ObservedOld, 150.0f);
    TestEqual(TEXT("属性变动同步触发蓝图动态多播"), DynamicReceiver->LastNewValue, 200.0f);

    Mapping->Bind(ASC2.Get(), HealthAttr, true);
    TestEqual(TEXT("切换绑定到新 ASC 并即时广播新值"), ObservedNew, 320.0f);
    TestEqual(TEXT("切换后绑定 ASC 指向 ASC2"), Mapping->GetBoundAbilitySystemComponent(), static_cast<UAbilitySystemComponent*>(ASC2.Get()));

    const int32 CountBeforeOldChange = NativeBroadcastCount;
    ASC1->SetNumericAttributeBase(HealthAttr, 99.0f);
    TestEqual(TEXT("切换绑定后旧 ASC 属性变更不再触发回调"), NativeBroadcastCount, CountBeforeOldChange);

    ASC2->SetNumericAttributeBase(HealthAttr, 280.0f);
    TestEqual(TEXT("切换绑定后新 ASC 属性变更正常触发"), ObservedNew, 280.0f);

    Mapping->Bind(nullptr, HealthAttr);
    TestTrue(TEXT("空 ASC 重新绑定安全短路保持原绑定状态"), Mapping->IsBound());
    TestEqual(TEXT("空 ASC 重新绑定数值保持"), Mapping->GetCurrentValue(), 280.0f);

    Mapping->Bind(ASC2.Get(), FGameplayAttribute());
    TestTrue(TEXT("空属性重新绑定安全短路保持原绑定状态"), Mapping->IsBound());
    TestEqual(TEXT("空属性重新绑定数值保持"), Mapping->GetCurrentValue(), 280.0f);

    Mapping->Unbind();
    TestEqual(TEXT("解绑后数值查询返回 0"), Mapping->GetCurrentValue(), 0.0f);
    TestFalse(TEXT("解绑后 IsBound 返回 false"), Mapping->IsBound());
    TestFalse(TEXT("解绑后 BoundAttribute 无效"), Mapping->GetBoundAttribute().IsValid());

    const int32 CountBeforeUnboundQuery = NativeBroadcastCount;
    Mapping->QueryAndBroadcast();
    TestEqual(TEXT("未绑定时 QueryAndBroadcast 静默跳过不广播虚假 0 值"), NativeBroadcastCount, CountBeforeUnboundQuery);

    TestNull(TEXT("空 ASC 创建映射返回 nullptr"), UGameplayAttributeValueMapping::CreateAttributeValueMapping(nullptr, nullptr, HealthAttr));
    TestNull(TEXT("空属性创建映射返回 nullptr"), UGameplayAttributeValueMapping::CreateAttributeValueMapping(nullptr, ASC1.Get(), FGameplayAttribute()));

    UGameplayAttributeValueMapping* CppConvenienceMapping = UGameplayAttributeValueMapping::CreateAttributeValueMapping(ASC1.Get(), HealthAttr);
    TestNotNull(TEXT("C++ 双参便利重载默认以 ASC 为 Outer 创建成功"), CppConvenienceMapping);
    TestTrue(TEXT("映射状态处于绑定中"), CppConvenienceMapping->IsBound());
    TestEqual(TEXT("绑定属性对齐"), CppConvenienceMapping->GetBoundAttribute(), HealthAttr);
    CppConvenienceMapping->Unbind();

    UGameAbilitySystemComponent* DisposableASC = NewObject<UGameAbilitySystemComponent>(GetTransientPackage());
    UAbilitySystemTestAttributeSet* DisposableAttrSet = NewObject<UAbilitySystemTestAttributeSet>(DisposableASC);
    DisposableASC->AddAttributeSetSubobject(DisposableAttrSet);
    DisposableASC->SetNumericAttributeBase(HealthAttr, 88.0f);

    Mapping->Bind(DisposableASC, HealthAttr, false);
    TestTrue(TEXT("绑定临时 ASC 后 IsBound 为 true"), Mapping->IsBound());

    DisposableASC->MarkAsGarbage();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestFalse(TEXT("目标 ASC 被 GC 销毁后 IsBound 必须返回 false"), Mapping->IsBound());
    TestNull(TEXT("目标 ASC 销毁后 GetBoundAbilitySystemComponent 返回 nullptr"), Mapping->GetBoundAbilitySystemComponent());
    TestEqual(TEXT("目标 ASC 销毁后 GetCurrentValue 返回 0.0f"), Mapping->GetCurrentValue(), 0.0f);

    const int32 CountBeforeExpiredQuery = NativeBroadcastCount;
    Mapping->QueryAndBroadcast();
    TestEqual(TEXT("目标 ASC 失效后 QueryAndBroadcast 不向 UI 误播 0.0f"), NativeBroadcastCount, CountBeforeExpiredQuery);
    Mapping->Unbind();

    IGamePoolable* Poolable = Cast<IGamePoolable>(Mapping.Get());
    TestNotNull(TEXT("属性映射实现 IGamePoolable 接口"), Poolable);
    Mapping->Bind(ASC2.Get(), HealthAttr, false);
    TestTrue(TEXT("入池前处于绑定态"), Mapping->IsBound());
    IGamePoolable::Execute_OnReleaseToPool(Mapping.Get());
    TestFalse(TEXT("归还对象池后自动解绑"), Mapping->IsBound());
    TestFalse(TEXT("归还对象池后蓝图多播订阅已清空"), Mapping->OnValueChanged.IsBound());
    TestFalse(TEXT("归还对象池后原生多播订阅已清空"), Mapping->OnValueChangedNative.IsBound());

    IGamePoolable::Execute_OnAcquireFromPool(Mapping.Get());
    int32 ReusedCalls = 0;
    Mapping->OnValueChangedNative.AddLambda([&ReusedCalls](float, float) {
        ++ReusedCalls;
    });
    Mapping->Bind(ASC2.Get(), HealthAttr, true);
    TestEqual(TEXT("出池复用重新绑定后即时广播生效"), ReusedCalls, 1);
    Mapping->Unbind();

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
