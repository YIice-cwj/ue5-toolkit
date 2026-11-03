/**
 * @file UGameGameplayAbilityTest.cpp
 * @brief GAS 能力基类（实例化策略、阻断状态标签否决、触发来源解析与复制）自动化测试套件
 */

#include "Ability/UGameGameplayAbility.h"

#if WITH_AUTOMATION_TESTS

#include "Ability/UGameAbilitySystemComponent.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayAbilityPolicyTest,
    "Game.Gameplay.AbilityPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 CDO 默认实例化策略（InstancedPerActor）与网络复制策略（ReplicateYes）
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameGameplayAbilityPolicyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const UGameGameplayAbility* CDO = GetDefault<UGameGameplayAbility>();
    TestNotNull(TEXT("Ability CDO 有效"), CDO);
    if (CDO == nullptr) {
        return false;
    }

    TestTrue(TEXT("默认实例化策略为 InstancedPerActor 以支持网络复制"),
        CDO->GetInstancingPolicy() == EGameplayAbilityInstancingPolicy::InstancedPerActor);
    TestTrue(TEXT("默认网络复制策略为 ReplicateYes"),
        CDO->GetReplicationPolicy() == EGameplayAbilityReplicationPolicy::ReplicateYes);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayAbilityActivationTest,
    "Game.Gameplay.AbilityActivation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证致死等状态标签阻断、相关标签收集、触发事件来源解析及属性网络复制注册
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameGameplayAbilityActivationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    AActor* DummyActor = NewObject<AActor>(GetTransientPackage());
    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(DummyActor);
    ASC->InitAbilityActorInfo(DummyActor, DummyActor);

    struct FScopedClearAbstract {
        UClass* ClassToModify;
        explicit FScopedClearAbstract(UClass* InClass) : ClassToModify(InClass) {
            ClassToModify->ClassFlags &= ~CLASS_Abstract;
        }
        ~FScopedClearAbstract() {
            ClassToModify->ClassFlags |= CLASS_Abstract;
        }
    };

    FGameplayAbilitySpecHandle AbilityHandle;
    {
        FScopedClearAbstract Guard(UGameGameplayAbility::StaticClass());
        AbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(UGameGameplayAbility::StaticClass(), 1, INDEX_NONE, DummyActor));
    }
    TestTrue(TEXT("成功授予能力句柄"), AbilityHandle.IsValid());

    FGameplayAbilitySpec* FoundSpec = ASC->FindAbilitySpecFromHandle(AbilityHandle);
    TestNotNull(TEXT("找到 AbilitySpec"), FoundSpec);
    if (FoundSpec == nullptr) {
        return false;
    }

    UGameGameplayAbility* AbilityInstance = Cast<UGameGameplayAbility>(FoundSpec->GetPrimaryInstance());
    TestNotNull(TEXT("成功获取实例化 Ability 对象"), AbilityInstance);
    if (AbilityInstance == nullptr) {
        return false;
    }

    const FGameplayAbilityActorInfo* ActorInfo = ASC->AbilityActorInfo.Get();
    TestNotNull(TEXT("ASC ActorInfo 有效"), ActorInfo);
    if (ActorInfo == nullptr) {
        return false;
    }

    TestTrue(TEXT("无阻断标签时激活允许"), AbilityInstance->CanActivateAbility(AbilityHandle, ActorInfo));

    FStructProperty* BlockingTagsProp = FindFProperty<FStructProperty>(
        UGameGameplayAbility::StaticClass(),
        TEXT("BlockingStateTags")
    );
    FMapProperty* TriggerTagMapProp = FindFProperty<FMapProperty>(
        UGameGameplayAbility::StaticClass(),
        TEXT("TriggerSourceTagMap")
    );
    TestNotNull(TEXT("反射找到 BlockingStateTags 属性"), BlockingTagsProp);
    TestNotNull(TEXT("反射找到 TriggerSourceTagMap 属性"), TriggerTagMapProp);
    if (BlockingTagsProp == nullptr || TriggerTagMapProp == nullptr) {
        return false;
    }

    auto& BlockingTags = *reinterpret_cast<FGameplayTagContainer*>(
        BlockingTagsProp->ContainerPtrToValuePtr<void>(AbilityInstance)
    );
    auto& TriggerTagMap = *reinterpret_cast<TMap<FGameplayTag, EGameAbilityTriggerSource>*>(
        TriggerTagMapProp->ContainerPtrToValuePtr<void>(AbilityInstance)
    );

    const FGameplayTag DeathTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Test.AbilityDead")));
    const FGameplayTag AITag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Event.Test.AbilityAI")));
    const FGameplayTag OtherTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Event.Test.AbilityOther")));

    BlockingTags.AddTag(DeathTag);
    TriggerTagMap.Add(AITag, EGameAbilityTriggerSource::AI);

    TestTrue(TEXT("宿主未持有致死标签时激活允许"), AbilityInstance->CanActivateAbility(AbilityHandle, ActorInfo));

    ASC->AddLooseGameplayTag(DeathTag);
    FGameplayTagContainer RelevantTags;
    TestFalse(TEXT("宿主持有致死标签时否决激活"), AbilityInstance->CanActivateAbility(AbilityHandle, ActorInfo, nullptr, nullptr, &RelevantTags));
    TestTrue(TEXT("相关标签中收集到致死标签"), RelevantTags.HasTag(DeathTag));
    TestFalse(TEXT("无相关标签输出参数时同样正确否决"), AbilityInstance->CanActivateAbility(AbilityHandle, ActorInfo, nullptr, nullptr, nullptr));

    ASC->RemoveLooseGameplayTag(DeathTag);
    TestTrue(TEXT("移除致死标签后恢复激活允许"), AbilityInstance->CanActivateAbility(AbilityHandle, ActorInfo));

    TArray<FLifetimeProperty> LifetimeProps;
    AbilityInstance->GetLifetimeReplicatedProps(LifetimeProps);
    TestTrue(TEXT("能力注册网络复制属性列表非空"), LifetimeProps.Num() > 0);

    FGameplayAbilityActivationInfo ActivationInfo;

    AbilityInstance->ActivateAbility(AbilityHandle, ActorInfo, ActivationInfo, nullptr);
    TestTrue(TEXT("空事件数据解析为 Input"), AbilityInstance->GetTriggerSource() == EGameAbilityTriggerSource::Input);

    FGameplayEventData AIEvent;
    AIEvent.EventTag = AITag;
    AbilityInstance->ActivateAbility(AbilityHandle, ActorInfo, ActivationInfo, &AIEvent);
    TestTrue(TEXT("命中映射表解析为 AI"), AbilityInstance->GetTriggerSource() == EGameAbilityTriggerSource::AI);

    FGameplayEventData OtherEvent;
    OtherEvent.EventTag = OtherTag;
    AbilityInstance->ActivateAbility(AbilityHandle, ActorInfo, ActivationInfo, &OtherEvent);
    TestTrue(TEXT("未在映射表中回落为 Effect"), AbilityInstance->GetTriggerSource() == EGameAbilityTriggerSource::Effect);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS

