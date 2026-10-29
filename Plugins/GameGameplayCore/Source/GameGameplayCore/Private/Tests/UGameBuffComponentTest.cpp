/**
 * @file UGameBuffComponentTest.cpp
 * @brief GameGameplayCore Buff 核心自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Buff/UGameBuffComponent.h"
#include "Stat/UGameStatComponent.h"

#if WITH_AUTOMATION_TESTS

namespace {

UWorld* GetTestWorld() {
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World() != nullptr) {
            return Context.World();
        }
    }
    return nullptr;
}

AActor* SpawnTestHost(UWorld* InWorld) {
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return InWorld->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffStackAndExpireTest,
    "Game.GameplayCore.Buff.StackAndExpire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameBuffStackAndExpireTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        return false;
    }
    AActor* HostActor = SpawnTestHost(World);
    UGameStatComponent* StatComponent = NewObject<UGameStatComponent>(HostActor);
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(HostActor);
    StatComponent->RegisterComponent();
    BuffComponent->RegisterComponent();
    BuffComponent->BindStatComponent(StatComponent);

    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Buff.Attack")));
    StatComponent->RegisterStat(AttackTag, 100.0f, 100.0f, 0.0f);

    FGameBuffSpec Spec;
    Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.AttackUp.Test")));
    Spec.Duration = 2.0f;
    Spec.MaxStacks = 2;
    Spec.StackPolicy = EGameBuffStackPolicy::RefreshDuration;

    FGameStatModifierSpec ModifierSpec;
    ModifierSpec.StatTag = AttackTag;
    ModifierSpec.Modifier = FGameStatModifier(10.0f, EGameStatModifierOp::Add);
    Spec.Modifiers.Add(ModifierSpec);

    int32 ChangedCount = 0;
    BuffComponent->OnBuffChangedNative.AddLambda([&ChangedCount](const FGameplayTag&, int32) {
        ++ChangedCount;
    });

    const int32 FirstInstanceId = BuffComponent->ApplyBuff(Spec);
    TestNotEqual(TEXT("Buff 施加应返回有效实例 Id"), FirstInstanceId, 0);
    TestTrue(TEXT("Buff 已激活"), BuffComponent->HasBuff(Spec.BuffTag));
    TestEqual(TEXT("首层叠层数为 1"), BuffComponent->GetBuffStacks(Spec.BuffTag), 1);
    TestEqual(TEXT("单层修正器派生值正确"), StatComponent->GetStatEffectiveCurrent(AttackTag), 110.0f);

    const int32 SecondInstanceId = BuffComponent->ApplyBuff(Spec);
    TestEqual(TEXT("同 Tag 非独立策略复用实例"), SecondInstanceId, FirstInstanceId);
    TestEqual(TEXT("叠层数增至 2"), BuffComponent->GetBuffStacks(Spec.BuffTag), 2);
    TestEqual(TEXT("双层修正器派生值按层数放大"), StatComponent->GetStatEffectiveCurrent(AttackTag), 120.0f);
    TestEqual(TEXT("每次施加都触发变更事件"), ChangedCount, 2);

    BuffComponent->TickComponent(1.5f, LEVELTICK_TimeOnly, nullptr);
    TestTrue(TEXT("未到期时 Buff 仍激活"), BuffComponent->HasBuff(Spec.BuffTag));

    BuffComponent->TickComponent(0.6f, LEVELTICK_TimeOnly, nullptr);
    TestFalse(TEXT("到期后 Buff 自动移除"), BuffComponent->HasBuff(Spec.BuffTag));
    TestEqual(TEXT("到期后修正器完整回滚"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);

    HostActor->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffCooldownStatusTest,
    "Game.GameplayCore.Buff.CooldownAsStatus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameBuffCooldownStatusTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        return false;
    }
    AActor* HostActor = SpawnTestHost(World);
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(HostActor);
    BuffComponent->RegisterComponent();

    const FGameplayTag CooldownStateTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Cooldown.TestSkill")));

    FGameBuffSpec CooldownSpec;
    CooldownSpec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Cooldown.TestSkill")));
    CooldownSpec.Duration = 1.0f;
    CooldownSpec.GrantedTags.AddTag(CooldownStateTag);

    const int32 InstanceId = BuffComponent->ApplyBuff(CooldownSpec);
    TestNotEqual(TEXT("冷却 Buff 施加成功"), InstanceId, 0);

    FGameplayTagContainer GrantedTags;
    BuffComponent->GetGrantedTags(GrantedTags);
    TestTrue(TEXT("冷却期间聚合状态标签生效"), GrantedTags.HasTag(CooldownStateTag));
    TestEqual(TEXT("冷却剩余时长读取正确"), BuffComponent->GetBuffRemainingTime(CooldownSpec.BuffTag), 1.0f);

    BuffComponent->TickComponent(0.5f, LEVELTICK_TimeOnly, nullptr);
    TestTrue(TEXT("冷却未结束时状态标签保持"), BuffComponent->HasBuff(CooldownSpec.BuffTag));

    BuffComponent->TickComponent(0.6f, LEVELTICK_TimeOnly, nullptr);
    TestFalse(TEXT("冷却结束时 Buff 移除"), BuffComponent->HasBuff(CooldownSpec.BuffTag));

    BuffComponent->GetGrantedTags(GrantedTags);
    TestFalse(TEXT("冷却结束后状态标签撤销"), GrantedTags.HasTag(CooldownStateTag));

    HostActor->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffReplicationCallbacksTest,
    "Game.GameplayCore.Buff.ReplicationCallbacks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameBuffReplicationCallbacksTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* StatComponent = NewObject<UGameStatComponent>(GetTransientPackage());
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(GetTransientPackage());
    BuffComponent->BindStatComponent(StatComponent);

    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Buff.Replicated")));
    StatComponent->RegisterStat(AttackTag, 100.0f, 100.0f, 0.0f);

    int32 ChangedStacks = INDEX_NONE;
    int32 RemovedCount = 0;
    BuffComponent->OnBuffChangedNative.AddLambda([&ChangedStacks](const FGameplayTag&, int32 InStacks) {
        ChangedStacks = InStacks;
    });
    BuffComponent->OnBuffRemovedNative.AddLambda([&RemovedCount](const FGameplayTag&) {
        ++RemovedCount;
    });

    FGameActiveBuffArray ReplicatedArray;
    ReplicatedArray.OwnerComponent = BuffComponent;

    FGameBuffInstance& Instance = ReplicatedArray.Items.AddDefaulted_GetRef();
    Instance.InstanceId = 1;
    Instance.Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Replicated.Test")));
    Instance.Stacks = 1;

    FGameStatModifierSpec ModifierSpec;
    ModifierSpec.StatTag = AttackTag;
    ModifierSpec.Modifier = FGameStatModifier(10.0f, EGameStatModifierOp::Add);
    Instance.Spec.Modifiers.Add(ModifierSpec);

    int32 Index = 0;
    ReplicatedArray.PostReplicatedAdd(TArrayView<int32>(&Index, 1), 1);
    TestEqual(TEXT("新增条目挂接修正器"), StatComponent->GetStatEffectiveCurrent(AttackTag), 110.0f);
    TestEqual(TEXT("新增条目广播叠层"), ChangedStacks, 1);

    ReplicatedArray.Items[0].Stacks = 2;
    ReplicatedArray.PostReplicatedChange(TArrayView<int32>(&Index, 1), 1);
    TestEqual(TEXT("变更条目按新叠层重建修正器"), StatComponent->GetStatEffectiveCurrent(AttackTag), 120.0f);
    TestEqual(TEXT("变更条目广播新叠层"), ChangedStacks, 2);

    ReplicatedArray.PreReplicatedRemove(TArrayView<int32>(&Index, 1), 0);
    TestEqual(TEXT("移除条目回滚修正器"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);
    TestEqual(TEXT("移除条目广播移除事件"), RemovedCount, 1);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffIndependentInstancesTest,
    "Game.GameplayCore.Buff.IndependentInstances",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameBuffIndependentInstancesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        return false;
    }
    AActor* HostActor = SpawnTestHost(World);
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(HostActor);
    BuffComponent->RegisterComponent();
    const FGameplayTag PoisonTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Independent.Poison")));

    int32 LastChangedStacks = 0;
    int32 RemovedBroadcastCount = 0;
    BuffComponent->OnBuffChangedNative.AddLambda([&LastChangedStacks](const FGameplayTag&, int32 InStacks) {
        LastChangedStacks = InStacks;
    });
    BuffComponent->OnBuffRemovedNative.AddLambda([&RemovedBroadcastCount](const FGameplayTag&) {
        ++RemovedBroadcastCount;
    });

    FGameBuffSpec ShortSpec;
    ShortSpec.BuffTag = PoisonTag;
    ShortSpec.Duration = 1.0f;
    ShortSpec.StackPolicy = EGameBuffStackPolicy::Independent;
    ShortSpec.MaxStacks = 5;

    FGameBuffSpec LongSpec = ShortSpec;
    LongSpec.Duration = 2.5f;

    const int32 FirstId = BuffComponent->ApplyBuff(ShortSpec);
    const int32 SecondId = BuffComponent->ApplyBuff(LongSpec);
    TestNotEqual(TEXT("独立策略应创建不同实例 Id"), FirstId, SecondId);
    TestEqual(TEXT("双独立实例聚合层数为 2"), BuffComponent->GetBuffStacks(PoisonTag), 2);
    TestEqual(TEXT("施加第二层时广播总层数 2"), LastChangedStacks, 2);

    BuffComponent->TickComponent(1.2f, LEVELTICK_TimeOnly, nullptr);
    TestTrue(TEXT("首个实例到期后同 Tag 仍激活"), BuffComponent->HasBuff(PoisonTag));
    TestEqual(TEXT("首个实例到期后剩余层数为 1"), BuffComponent->GetBuffStacks(PoisonTag), 1);
    TestEqual(TEXT("单个独立实例到期应广播层数变更而非移除"), LastChangedStacks, 1);
    TestEqual(TEXT("仍有同 Tag 实例存活时不应广播 Removed"), RemovedBroadcastCount, 0);

    BuffComponent->ApplyBuff(LongSpec);
    TestEqual(TEXT("再次施加独立实例后层数为 2"), BuffComponent->GetBuffStacks(PoisonTag), 2);
    TestTrue(TEXT("单实例移除成功"), BuffComponent->RemoveBuff(PoisonTag, false));
    TestEqual(TEXT("单实例移除后剩余层数为 1"), BuffComponent->GetBuffStacks(PoisonTag), 1);
    TestEqual(TEXT("单实例移除后未触发 Removed"), RemovedBroadcastCount, 0);

    BuffComponent->TickComponent(2.0f, LEVELTICK_TimeOnly, nullptr);
    TestFalse(TEXT("全部实例到期后 Buff 不再激活"), BuffComponent->HasBuff(PoisonTag));
    TestEqual(TEXT("最后一个实例到期后触发一次 Removed"), RemovedBroadcastCount, 1);

    FGameBuffSpec CappedSpec;
    CappedSpec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Independent.Capped")));
    CappedSpec.Duration = 5.0f;
    CappedSpec.StackPolicy = EGameBuffStackPolicy::Independent;
    CappedSpec.MaxStacks = 2;

    TestNotEqual(TEXT("首实例施加成功"), BuffComponent->ApplyBuff(CappedSpec), 0);
    TestNotEqual(TEXT("第二实例施加成功"), BuffComponent->ApplyBuff(CappedSpec), 0);
    TestEqual(TEXT("超出 MaxStacks 时第三实例应被拒绝"), BuffComponent->ApplyBuff(CappedSpec), 0);
    TestEqual(TEXT("独立实例总层数保持为 2"), BuffComponent->GetBuffStacks(CappedSpec.BuffTag), 2);

    HostActor->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffRemoveStacksTest,
    "Game.GameplayCore.Buff.RemoveStacks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameBuffRemoveStacksTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(GetTransientPackage());
    const FGameplayTag BleedTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Test.Bleed")));

    FGameBuffSpec Spec;
    Spec.BuffTag = BleedTag;
    Spec.Duration = 10.0f;
    Spec.MaxStacks = 5;
    Spec.StackPolicy = EGameBuffStackPolicy::RefreshDuration;

    BuffComponent->ApplyBuff(Spec);
    BuffComponent->ApplyBuff(Spec);
    BuffComponent->ApplyBuff(Spec);
    TestEqual(TEXT("三层初始生效"), BuffComponent->GetBuffStacks(BleedTag), 3);

    TestTrue(TEXT("削减 1 层成功"), BuffComponent->RemoveBuffStacks(BleedTag, 1));
    TestEqual(TEXT("削减后剩余 2 层"), BuffComponent->GetBuffStacks(BleedTag), 2);
    TestTrue(TEXT("Buff 仍存活"), BuffComponent->HasBuff(BleedTag));

    TestTrue(TEXT("削减 2 层成功"), BuffComponent->RemoveBuffStacks(BleedTag, 2));
    TestEqual(TEXT("削减后剩余 0 层"), BuffComponent->GetBuffStacks(BleedTag), 0);
    TestFalse(TEXT("层数归零后 Buff 移除"), BuffComponent->HasBuff(BleedTag));

    return true;
}

#endif
