/**
 * @file FGameActiveBuffArrayTest.cpp
 * @brief FGameActiveBuffArray FastArray 网络增量复制与回调自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Buff/FGameActiveBuffArray.h"
#include "Buff/UGameBuffComponent.h"
#include "Stat/UGameStatComponent.h"

#if WITH_AUTOMATION_TESTS

namespace {

UWorld* GetActiveBuffArrayTestWorld() {
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World() != nullptr) {
            return Context.World();
        }
    }
    return nullptr;
}

AActor* SpawnActiveBuffArrayTestHost(UWorld* InWorld) {
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return InWorld->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffReplicationCallbacksTest,
    "Game.GameplayCore.Buff.ReplicationCallbacks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameActiveBuffArray 在真实 BuffComponent 上的增/改/删复制回调与属性修正器无损重建
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
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

    FGameBuffInstance& Instance = BuffComponent->ActiveBuffs.Items.AddDefaulted_GetRef();
    Instance.InstanceId = 1;
    Instance.Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Replicated.Test")));
    Instance.Stacks = 1;

    FGameStatModifierSpec ModifierSpec;
    ModifierSpec.StatTag = AttackTag;
    ModifierSpec.Modifier = FGameStatModifier(10.0f, EGameStatModifierOp::Add);
    Instance.Spec.Modifiers.Add(ModifierSpec);

    int32 Index = 0;
    BuffComponent->ActiveBuffs.PostReplicatedAdd(TArrayView<int32>(&Index, 1), 1);
    TestEqual(TEXT("新增条目挂接修正器"), StatComponent->GetStatEffectiveCurrent(AttackTag), 110.0f);
    TestEqual(TEXT("新增条目广播叠层"), ChangedStacks, 1);

    BuffComponent->ActiveBuffs.Items[0].Stacks = 2;
    BuffComponent->ActiveBuffs.PostReplicatedChange(TArrayView<int32>(&Index, 1), 1);
    TestEqual(TEXT("变更条目按新叠层重建修正器"), StatComponent->GetStatEffectiveCurrent(AttackTag), 120.0f);
    TestEqual(TEXT("变更条目广播新叠层"), ChangedStacks, 2);

    BuffComponent->ActiveBuffs.PreReplicatedRemove(TArrayView<int32>(&Index, 1), 0);
    BuffComponent->ActiveBuffs.Items.RemoveAt(0);
    TestEqual(TEXT("移除条目回滚修正器"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);
    TestEqual(TEXT("移除条目广播移除事件"), RemovedCount, 1);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffReplicatedSpecChangeTest,
    "Game.GameplayCore.Buff.ReplicatedSpecChange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证复制条目叠层未变但 Spec 规格变化时的差分识别与广播
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffReplicatedSpecChangeTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(GetTransientPackage());
    const FGameplayTag BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.ReplicatedSpecChange.Test")));
    int32 ChangedCount = 0;
    BuffComponent->OnBuffChangedNative.AddLambda([BuffTag, &ChangedCount](const FGameplayTag& ChangedTag, int32) {
        if (ChangedTag.MatchesTagExact(BuffTag)) {
            ++ChangedCount;
        }
    });

    FGameActiveBuffArray ReplicatedArray;
    ReplicatedArray.OwnerComponent = BuffComponent;
    FGameBuffInstance& Instance = ReplicatedArray.Items.AddDefaulted_GetRef();
    Instance.InstanceId = 1;
    Instance.Spec.BuffTag = BuffTag;
    Instance.Stacks = 1;

    int32 Index = 0;
    ReplicatedArray.PostReplicatedAdd(TArrayView<int32>(&Index, 1), 1);
    TestEqual(TEXT("新增复制条目广播一次"), ChangedCount, 1);

    Instance.Spec.GrantedTags.AddTag(UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.ReplicatedSpecChange"))));
    ReplicatedArray.PostReplicatedChange(TArrayView<int32>(&Index, 1), 1);
    TestEqual(TEXT("叠层不变但 Spec 变化仍恰好广播一次"), ChangedCount, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffReplicationEmptyTickTest,
    "Game.GameplayCore.Buff.ReplicationEmptyTick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证网络复制移除最后一个 Buff 后，空容器防抖 Tick 能够自适应关闭
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffReplicationEmptyTickTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetActiveBuffArrayTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        return false;
    }

    AActor* HostActor = SpawnActiveBuffArrayTestHost(World);
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(HostActor);
    BuffComponent->RegisterComponent();
    FGameActiveBuffArray ReplicatedArray;
    ReplicatedArray.OwnerComponent = BuffComponent;
    FGameBuffInstance& Instance = ReplicatedArray.Items.AddDefaulted_GetRef();
    Instance.InstanceId = 1;
    Instance.Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.ReplicationEmptyTick.Test")));
    Instance.Spec.Duration = 1.0f;
    Instance.RemainingSeconds = 1.0f;

    int32 Index = 0;
    ReplicatedArray.PostReplicatedAdd(TArrayView<int32>(&Index, 1), 1);
    BuffComponent->SetComponentTickEnabled(true);
    ReplicatedArray.PreReplicatedRemove(TArrayView<int32>(&Index, 1), 0);
    ReplicatedArray.Items.RemoveAt(Index);
    BuffComponent->TickComponent(0.1f, LEVELTICK_TimeOnly, nullptr);
    TestFalse(TEXT("复制删除最后 Buff 后空 Tick 自动关闭"), BuffComponent->IsComponentTickEnabled());
    HostActor->Destroy();
    return true;
}

#endif
