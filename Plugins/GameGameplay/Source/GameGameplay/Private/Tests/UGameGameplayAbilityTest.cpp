/**
 * @file UGameGameplayAbilityTest.cpp
 * @brief GAS 能力基类（实例化/复制策略、阻断状态标签零分配收集、触发来源解析与结束复位）自动化测试套件
 */

#include "Ability/UGameGameplayAbility.h"

#if WITH_AUTOMATION_TESTS

#include "Ability/UGameAbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayAbilityPolicyTest,
    "Game.Gameplay.AbilityPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 CDO 默认实例化策略（InstancedPerActor）与复制策略（ReplicateNo，零多余 Subobject 复制开销）
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameGameplayAbilityPolicyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const UGameGameplayAbility* CDO = GetDefault<UGameGameplayAbility>();
    TestNotNull(TEXT("Ability CDO 有效"), CDO);
    if (CDO == nullptr) {
        return false;
    }

    TestTrue(
        TEXT("默认实例化策略为 InstancedPerActor"),
        CDO->GetInstancingPolicy() == EGameplayAbilityInstancingPolicy::InstancedPerActor
    );
    TestTrue(
        TEXT("默认网络复制策略为 ReplicateNo（消除全项目 Subobject 复制开销）"),
        CDO->GetReplicationPolicy() == EGameplayAbilityReplicationPolicy::ReplicateNo
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayAbilityActivationTest,
    "Game.Gameplay.AbilityActivation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多状态阻断标签精准收集、边界空指针安全、触发来源解析与 EndAbility 触发源复位
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameGameplayAbilityActivationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* TestWorld = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("能力测试世界有效"), TestWorld);
    if (TestWorld == nullptr) {
        return false;
    }
    ON_SCOPE_EXIT {
        TestWorld->DestroyWorld(false);
    };

    AActor* DummyActor = TestWorld->SpawnActor<AActor>();
    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(DummyActor);
    DummyActor->AddInstanceComponent(ASC);
    ASC->RegisterComponent();
    ASC->InitAbilityActorInfo(DummyActor, DummyActor);

    struct FScopedClearAbstract {
        UClass* ClassToModify;
        explicit FScopedClearAbstract(UClass* InClass)
            : ClassToModify(InClass) {
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
    TestFalse(TEXT("ActorInfo 为空时基类判定拒绝激活"), AbilityInstance->CanActivateAbility(AbilityHandle, nullptr));

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
    const FGameplayTag StunTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Test.AbilityStun")));
    const FGameplayTag SilenceTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Test.AbilitySilence")));
    const FGameplayTag AITag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Event.Test.AbilityAI")));
    const FGameplayTag OtherTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Event.Test.AbilityOther")));

    BlockingTags.AddTag(DeathTag);
    BlockingTags.AddTag(StunTag);
    BlockingTags.AddTag(SilenceTag);
    TriggerTagMap.Add(AITag, EGameAbilityTriggerSource::AI);

    TestTrue(TEXT("宿主未持有阻断标签时激活允许"), AbilityInstance->CanActivateAbility(AbilityHandle, ActorInfo));

    ASC->AddLooseGameplayTag(DeathTag);
    ASC->AddLooseGameplayTag(StunTag);
    FGameplayTagContainer RelevantTags;
    TestFalse(
        TEXT("宿主持有阻断标签时否决激活"),
        AbilityInstance->CanActivateAbility(AbilityHandle, ActorInfo, nullptr, nullptr, &RelevantTags)
    );
    TestTrue(TEXT("精准收集到命中的致死标签"), RelevantTags.HasTag(DeathTag));
    TestTrue(TEXT("精准收集到命中的眩晕标签"), RelevantTags.HasTag(StunTag));
    TestFalse(TEXT("未持有的沉默标签不误入 RelevantTags"), RelevantTags.HasTag(SilenceTag));
    TestFalse(
        TEXT("无相关标签输出参数时同样正确否决"),
        AbilityInstance->CanActivateAbility(AbilityHandle, ActorInfo, nullptr, nullptr, nullptr)
    );

    ASC->RemoveLooseGameplayTag(DeathTag);
    ASC->RemoveLooseGameplayTag(StunTag);
    TestTrue(TEXT("移除阻断标签后恢复激活允许"), AbilityInstance->CanActivateAbility(AbilityHandle, ActorInfo));

    FGameplayAbilityActivationInfo ActivationInfo;
    AbilityInstance->ActivateAbility(AbilityHandle, ActorInfo, ActivationInfo, nullptr);
    TestTrue(TEXT("空事件数据解析为 Input"), AbilityInstance->GetTriggerSource() == EGameAbilityTriggerSource::Input);

    FGameplayEventData AIEvent;
    AIEvent.EventTag = AITag;
    EGameAbilityTriggerSource SourceObservedDuringEndCallback = EGameAbilityTriggerSource::Input;
    int32 EndCallbackCount = 0;
    const FDelegateHandle EndDelegateHandle = AbilityInstance->OnGameplayAbilityEnded.AddLambda(
        [&SourceObservedDuringEndCallback, &EndCallbackCount](UGameplayAbility* EndedAbility) {
            if (const UGameGameplayAbility* GameAbility = Cast<UGameGameplayAbility>(EndedAbility)) {
                SourceObservedDuringEndCallback = GameAbility->GetTriggerSource();
            }
            ++EndCallbackCount;
        }
    );

    const bool bTriggeredByEvent = ASC->TriggerAbilityFromGameplayEvent(
        AbilityHandle,
        ASC->AbilityActorInfo.Get(),
        AITag,
        &AIEvent,
        *ASC
    );
    TestTrue(TEXT("通过 GameplayEvent 真实激活能力成功"), bTriggeredByEvent);
    TestTrue(TEXT("命中映射表解析为 AI"), AbilityInstance->GetTriggerSource() == EGameAbilityTriggerSource::AI);
    TestTrue(TEXT("事件激活后能力处于活跃状态"), AbilityInstance->IsActive());

    AbilityInstance->EndAbility(AbilityHandle, ActorInfo, AbilityInstance->GetCurrentActivationInfo(), false, false);
    AbilityInstance->OnGameplayAbilityEnded.Remove(EndDelegateHandle);
    TestEqual(TEXT("EndAbility 回调触发 1 次"), EndCallbackCount, 1);
    TestTrue(
        TEXT("EndAbility 回调期间仍可观测到本次真实触发来源 AI（绝不提前清零）"),
        SourceObservedDuringEndCallback == EGameAbilityTriggerSource::AI
    );
    TestFalse(TEXT("EndAbility 后能力不再活跃"), AbilityInstance->IsActive());
    TestTrue(TEXT("EndAbility 全部结束后触发来源复位为 Input"), AbilityInstance->GetTriggerSource() == EGameAbilityTriggerSource::Input);

    FGameplayEventData OtherEvent;
    OtherEvent.EventTag = OtherTag;
    AbilityInstance->ActivateAbility(AbilityHandle, ActorInfo, ActivationInfo, &OtherEvent);
    TestTrue(TEXT("未在映射表中回落为 Effect"), AbilityInstance->GetTriggerSource() == EGameAbilityTriggerSource::Effect);

    AbilityInstance->EndAbility(AbilityHandle, ActorInfo, ActivationInfo, false, true);
    TestTrue(TEXT("非活跃结束调用后触发来源同样复位为 Input"), AbilityInstance->GetTriggerSource() == EGameAbilityTriggerSource::Input);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
