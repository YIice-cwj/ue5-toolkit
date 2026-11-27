/**
 * @file UGameAbilitySystemComponentTest.cpp
 * @brief 通用能力系统组件基类与对象池复用生命周期自动化测试套件
 */

#include "Ability/UGameAbilitySystemComponent.h"

#if WITH_AUTOMATION_TESTS

#include "Abilities/GameplayAbility.h"
#include "Core/IGamePoolable.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

namespace {

class FTestGameASCAccessor : public UGameAbilitySystemComponent {
public:
    static int32 GetMinimalRepTagCount(const UGameAbilitySystemComponent* InASC) {
        return static_cast<const FTestGameASCAccessor*>(InASC)->GetMinimalReplicationTags().TagMap.Num();
    }

    static int32 GetReplicatedLooseTagCount(const UGameAbilitySystemComponent* InASC) {
        return static_cast<const FTestGameASCAccessor*>(InASC)->GetReplicatedLooseTags().TagMap.Num();
    }

    static void AddReplicatedLooseTagDirect(UGameAbilitySystemComponent* InASC, const FGameplayTag& InTag) {
        static_cast<FTestGameASCAccessor*>(InASC)->GetReplicatedLooseTags_Mutable().AddTag(InTag);
    }
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAbilitySystemComponentTest,
    "Game.Gameplay.AbilitySystemComponent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 ASC 默认网络复制、InitAbilityActorInfo 动态分配宿主绑定及 IGamePoolable 完整归还复用
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameAbilitySystemComponentTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* TestWorld = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("测试世界创建成功"), TestWorld);
    if (TestWorld == nullptr) {
        return false;
    }
    ON_SCOPE_EXIT {
        TestWorld->DestroyWorld(false);
    };

    AActor* OwnerActor = TestWorld->SpawnActor<AActor>();
    AActor* AvatarActor = TestWorld->SpawnActor<AActor>();
    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(OwnerActor);
    TestNotNull(TEXT("ASC 创建成功"), ASC);
    if (ASC == nullptr) {
        return false;
    }

    TestTrue(TEXT("ASC 默认开启网络复制"), ASC->GetIsReplicated());
    TestFalse(TEXT("初始化前 AbilityActorInfo 为空"), ASC->AbilityActorInfo.IsValid());

    IGamePoolable* Poolable = Cast<IGamePoolable>(ASC);
    TestNotNull(TEXT("ASC 实现 IGamePoolable 接口"), Poolable);
    IGamePoolable::Execute_OnReleaseToPool(ASC);
    IGamePoolable::Execute_OnAcquireFromPool(ASC);
    TestFalse(TEXT("未初始化 ActorInfo 时直接入池/出池安全不崩溃"), ASC->AbilityActorInfo.IsValid());

    OwnerActor->AddInstanceComponent(ASC);
    ASC->RegisterComponent();
    ASC->InitAbilityActorInfo(OwnerActor, AvatarActor);
    TestTrue(TEXT("初始化后 AbilityActorInfo 分配就绪"), ASC->AbilityActorInfo.IsValid());
    if (ASC->AbilityActorInfo.IsValid()) {
        TestEqual(TEXT("AbilityActorInfo OwnerActor 绑定正确"), ASC->AbilityActorInfo->OwnerActor.Get(), OwnerActor);
        TestEqual(TEXT("AbilityActorInfo AvatarActor 绑定正确"), ASC->AbilityActorInfo->AvatarActor.Get(), AvatarActor);
    }

    const FGameplayAbilitySpecHandle GrantedHandle = ASC->GiveAbility(FGameplayAbilitySpec(UGameplayAbility::StaticClass(), 1, INDEX_NONE, OwnerActor));
    TestTrue(TEXT("池化前成功授予能力"), GrantedHandle.IsValid());

    const FGameplayTag LooseTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Test.AscPoolLoose")));
    const FGameplayTag MinimalRepTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Test.AscPoolMinRep")));
    const FGameplayTag ReplicatedLooseTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Test.AscPoolRepLoose")));
    const FGameplayTag BlockedAbilityTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Ability.Test.AscPoolBlocked")));
    const FGameplayTag PersistentCueTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("GameplayCue.Test.AscPoolBurn")));
    ASC->AddLooseGameplayTag(LooseTag);
    ASC->AddMinimalReplicationGameplayTag(MinimalRepTag);
    FTestGameASCAccessor::AddReplicatedLooseTagDirect(ASC, ReplicatedLooseTag);
    FGameplayTagContainer BlockedContainer;
    BlockedContainer.AddTag(BlockedAbilityTag);
    ASC->BlockAbilitiesWithTags(BlockedContainer);
    ASC->AddGameplayCue(PersistentCueTag);
    TestTrue(TEXT("池化前持有松标签"), ASC->HasMatchingGameplayTag(LooseTag));
    TestTrue(TEXT("池化前持有最小复制标签"), FTestGameASCAccessor::GetMinimalRepTagCount(ASC) > 0);
    TestTrue(TEXT("池化前持有复制松标签"), FTestGameASCAccessor::GetReplicatedLooseTagCount(ASC) > 0);
    TestTrue(TEXT("池化前阻塞能力标签生效"), ASC->AreAbilityTagsBlocked(BlockedContainer));
    TestTrue(TEXT("池化前持续 GameplayCue 处于激活态"), ASC->IsGameplayCueActive(PersistentCueTag));

    UGameplayEffect* InfiniteEffect = NewObject<UGameplayEffect>(GetTransientPackage());
    InfiniteEffect->DurationPolicy = EGameplayEffectDurationType::Infinite;
    const FActiveGameplayEffectHandle ActiveEffect = ASC->ApplyGameplayEffectToSelf(InfiniteEffect, 1.0f, ASC->MakeEffectContext());
    TestTrue(TEXT("池化前成功施加无限时长效果"), ActiveEffect.IsValid());
    TestEqual(TEXT("池化前活跃效果数量为 1"), ASC->GetActiveEffects(FGameplayEffectQuery()).Num(), 1);

    IGamePoolable::Execute_OnReleaseToPool(ASC);
    TestEqual(TEXT("归还对象池后已授予能力全部清空"), ASC->GetActivatableAbilities().Num(), 0);
    TestEqual(TEXT("归还对象池后活跃效果全部清空"), ASC->GetActiveEffects(FGameplayEffectQuery()).Num(), 0);
    TestFalse(TEXT("归还对象池后持续 GameplayCue 已移除"), ASC->IsGameplayCueActive(PersistentCueTag));
    TestEqual(TEXT("归还对象池后最小复制标签映射已清空"), FTestGameASCAccessor::GetMinimalRepTagCount(ASC), 0);
    TestEqual(TEXT("归还对象池后复制松标签映射已清空"), FTestGameASCAccessor::GetReplicatedLooseTagCount(ASC), 0);
    TestFalse(TEXT("归还对象池后阻塞能力标签已清空"), ASC->AreAbilityTagsBlocked(BlockedContainer));
    TestFalse(TEXT("归还对象池后松标签计数静默归零"), ASC->HasMatchingGameplayTag(LooseTag));
    TestTrue(TEXT("归还对象池后显式 OwnedGameplayTags 容器亦彻底清空"), ASC->GetOwnedGameplayTags().IsEmpty());
    TestNull(TEXT("归还对象池后 OwnerActor 已清空"), ASC->GetOwnerActor());
    TestNull(TEXT("归还对象池后 AvatarActor 已清空"), ASC->GetAvatarActor());

    IGamePoolable::Execute_OnAcquireFromPool(ASC);
    ASC->InitAbilityActorInfo(OwnerActor, AvatarActor);
    TestEqual(TEXT("出池重新握手后 OwnerActor 恢复绑定"), ASC->GetOwnerActor(), OwnerActor);
    const FGameplayAbilitySpecHandle ReGrantedHandle = ASC->GiveAbility(FGameplayAbilitySpec(UGameplayAbility::StaticClass(), 1, INDEX_NONE, OwnerActor));
    TestTrue(TEXT("出池重新握手后可再次正常授予能力"), ReGrantedHandle.IsValid());

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
