/**
 * @file FGameActiveBuffArrayTest.cpp
 * @brief FGameActiveBuffArray FastArray 网络增量复制与回调自动化测试
 */

#include "CoreMinimal.h"
#include "Buff/FGameActiveBuffArray.h"
#include "Buff/UGameBuffComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Stat/UGameStatComponent.h"
#include "Tests/GameGameplayCoreTestUtils.h"

#if WITH_AUTOMATION_TESTS

using Game::GameplayCore::Test::FScopedTestHostActor;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffReplicationCallbacksTest,
    "Game.GameplayCore.Buff.ReplicationCallbacks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameActiveBuffArray 在真实 BuffComponent 上的增/改/删复制回调、原子修正器同步与当帧关停 Tick
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffReplicationCallbacksTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestHostActor Host;
    TestTrue(TEXT("宿主 Actor 有效"), Host.IsValidHost());
    if (!Host.IsValidHost()) {
        return false;
    }

    UGameStatComponent* StatComponent = NewObject<UGameStatComponent>(Host.Get());
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(Host.Get());
    StatComponent->RegisterComponent();
    BuffComponent->RegisterComponent();
    BuffComponent->BindStatComponent(StatComponent);

    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Buff.Replicated")));
    StatComponent->RegisterStat(AttackTag, 100.0f, 100.0f, 0.0f);

    int32 ChangedStacks = INDEX_NONE;
    int32 RemovedCount = 0;
    int32 StatChangeCount = 0;
    BuffComponent->OnBuffChangedNative.AddLambda([&ChangedStacks](const FGameplayTag&, int32 InStacks) {
        ChangedStacks = InStacks;
    });
    BuffComponent->OnBuffRemovedNative.AddLambda([&RemovedCount](const FGameplayTag&) {
        ++RemovedCount;
    });
    StatComponent->OnStatChangedNative.AddLambda([&StatChangeCount](const FGameplayTag&, float, float, float) {
        ++StatChangeCount;
    });

    FGameBuffInstance& Instance = BuffComponent->ActiveBuffs.Items.AddDefaulted_GetRef();
    Instance.InstanceId = 1;
    Instance.Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Replicated.Test")));
    Instance.Spec.Duration = 5.0f;
    Instance.RemainingSeconds = 5.0f;
    Instance.Stacks = 1;

    FGameStatModifierSpec ModifierSpec;
    ModifierSpec.StatTag = AttackTag;
    ModifierSpec.Modifier = FGameStatModifier(10.0f, EGameStatModifierOp::Add);
    Instance.Spec.Modifiers.Add(ModifierSpec);

    int32 Index = 0;
    BuffComponent->ActiveBuffs.PostReplicatedAdd(TArrayView<int32>(&Index, 1), 1);
    TestEqual(TEXT("新增条目挂接修正器"), StatComponent->GetStatEffectiveCurrent(AttackTag), 110.0f);
    TestEqual(TEXT("新增条目广播叠层"), ChangedStacks, 1);
    TestTrue(TEXT("限时条目到达后开启 Tick"), BuffComponent->IsComponentTickEnabled());

    FGameBuffInstance& PermInstance = BuffComponent->ActiveBuffs.Items.AddDefaulted_GetRef();
    PermInstance.InstanceId = 2;
    PermInstance.Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Replicated.Permanent")));
    PermInstance.Spec.Duration = 0.0f;
    PermInstance.Stacks = 1;
    int32 PermIndex = 1;
    BuffComponent->ActiveBuffs.PostReplicatedAdd(TArrayView<int32>(&PermIndex, 1), 2);

    StatChangeCount = 0;
    BuffComponent->ActiveBuffs.Items[0].Stacks = 2;
    BuffComponent->ActiveBuffs.PostReplicatedChange(TArrayView<int32>(&Index, 1), 2);
    TestEqual(TEXT("变更条目按新叠层原子更新修正器"), StatComponent->GetStatEffectiveCurrent(AttackTag), 120.0f);
    TestEqual(TEXT("原子更新仅产生单次属性变更广播"), StatChangeCount, 1);
    TestEqual(TEXT("变更条目广播新叠层"), ChangedStacks, 2);

    BuffComponent->ActiveBuffs.PreReplicatedRemove(TArrayView<int32>(&Index, 1), 1);
    TestFalse(TEXT("仅剩永久 Buff 时 PreReplicatedRemove 当帧立即关闭 Tick 零空转"), BuffComponent->IsComponentTickEnabled());
    BuffComponent->ActiveBuffs.Items.RemoveAt(0);
    TestEqual(TEXT("移除条目回滚修正器"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);
    TestEqual(TEXT("移除条目广播移除事件"), RemovedCount, 1);

    FGameBuffInstance& BatchA = BuffComponent->ActiveBuffs.Items.AddDefaulted_GetRef();
    BatchA.InstanceId = 10;
    BatchA.Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Replicated.BatchA")));
    BatchA.Stacks = 1;
    BatchA.Spec.Modifiers.Add(ModifierSpec);

    FGameBuffInstance& BatchB = BuffComponent->ActiveBuffs.Items.AddDefaulted_GetRef();
    BatchB.InstanceId = 11;
    BatchB.Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Replicated.BatchB")));
    BatchB.Stacks = 1;
    BatchB.Spec.Modifiers.Add(ModifierSpec);

    int32 BatchIndices[2] = {1, 2};
    StatChangeCount = 0;
    BuffComponent->ActiveBuffs.PostReplicatedAdd(TArrayView<int32>(BatchIndices, 2), 3);
    TestEqual(TEXT("同包新增 2 个同属性 Buff 合并为单次 Stat 广播"), StatChangeCount, 1);
    TestEqual(TEXT("同包新增后有效攻击力为 120"), StatComponent->GetStatEffectiveCurrent(AttackTag), 120.0f);

    StatChangeCount = 0;
    BuffComponent->ActiveBuffs.Items[1].Stacks = 2;
    BuffComponent->ActiveBuffs.Items[2].Stacks = 2;
    BuffComponent->ActiveBuffs.PostReplicatedChange(TArrayView<int32>(BatchIndices, 2), 3);
    TestEqual(TEXT("同包变更 2 个同属性 Buff 合并为单次 Stat 广播"), StatChangeCount, 1);
    TestEqual(TEXT("同包变更后有效攻击力为 140"), StatComponent->GetStatEffectiveCurrent(AttackTag), 140.0f);

    StatChangeCount = 0;
    BuffComponent->ActiveBuffs.PreReplicatedRemove(TArrayView<int32>(BatchIndices, 2), 1);
    TestEqual(TEXT("同包移除 2 个同属性 Buff 合并为单次 Stat 广播"), StatChangeCount, 1);
    TestEqual(TEXT("同包移除后有效攻击力回落 100"), StatComponent->GetStatEffectiveCurrent(AttackTag), 100.0f);

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
 * @brief 验证网络复制移除最后一个 Buff 后，Tick 在 PreReplicatedRemove 当帧即关闭且空 Tick 具备自愈兜底
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffReplicationEmptyTickTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestHostActor Host;
    TestTrue(TEXT("测试宿主 Actor 有效"), Host.IsValidHost());
    if (!Host.IsValidHost()) {
        return false;
    }

    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(Host.Get());
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
    TestFalse(TEXT("复制删除最后 Buff 在 PreReplicatedRemove 当帧立即关闭 Tick"), BuffComponent->IsComponentTickEnabled());

    ReplicatedArray.Items.RemoveAt(Index);
    BuffComponent->SetComponentTickEnabled(true);
    BuffComponent->TickComponent(0.1f, LEVELTICK_TimeOnly, nullptr);
    TestFalse(TEXT("空容器 Tick 自愈兜底关闭"), BuffComponent->IsComponentTickEnabled());
    return true;
}

#endif
