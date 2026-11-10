/**
 * @file UGameBuffComponentTest.cpp
 * @brief UGameBuffComponent 核心生命周期、叠层策略与权限门禁自动化测试
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

UWorld* GetBuffTestWorld() {
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World() != nullptr) {
            return Context.World();
        }
    }
    return nullptr;
}

AActor* SpawnBuffTestHost(UWorld* InWorld) {
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

/**
 * @brief 验证 Buff 施加、叠层刷新持续时长、属性修正器倍率挂接及到期后自动移除回滚
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffStackAndExpireTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetBuffTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        return false;
    }
    AActor* HostActor = SpawnBuffTestHost(World);
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

    const TGameStatusResult<int32> FirstResult = BuffComponent->ApplyBuff(Spec);
    TestTrue(TEXT("Buff 施加应返回成功"), FirstResult.IsSuccess());
    const int32 FirstInstanceId = FirstResult.GetValue();
    TestTrue(TEXT("Buff 已激活"), BuffComponent->HasBuff(Spec.BuffTag));
    TestEqual(TEXT("首层叠层数为 1"), BuffComponent->GetBuffStacks(Spec.BuffTag), 1);
    TestEqual(TEXT("单层修正器派生值正确"), StatComponent->GetStatEffectiveCurrent(AttackTag), 110.0f);

    const TGameStatusResult<int32> SecondResult = BuffComponent->ApplyBuff(Spec);
    TestTrue(TEXT("Buff 二次施加应返回成功"), SecondResult.IsSuccess());
    const int32 SecondInstanceId = SecondResult.GetValue();
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

/**
 * @brief 验证以 Buff 承载技能冷却、持续期间授予状态标签并在冷却结束后自动清理标签
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffCooldownStatusTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetBuffTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        return false;
    }
    AActor* HostActor = SpawnBuffTestHost(World);
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(HostActor);
    BuffComponent->RegisterComponent();

    const FGameplayTag CooldownStateTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Cooldown.TestSkill")));

    FGameBuffSpec CooldownSpec;
    CooldownSpec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Cooldown.TestSkill")));
    CooldownSpec.Duration = 1.0f;
    CooldownSpec.GrantedTags.AddTag(CooldownStateTag);

    const TGameStatusResult<int32> InstanceResult = BuffComponent->ApplyBuff(CooldownSpec);
    TestTrue(TEXT("冷却 Buff 施加成功"), InstanceResult.IsSuccess());
    const int32 InstanceId = InstanceResult.GetValue();

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
    FGameBuffIndependentInstancesTest,
    "Game.GameplayCore.Buff.IndependentInstances",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证独立实例策略下各 Buff 实例独立计时、按到期顺序削减层数及 MaxStacks 堆叠上限拦截
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffIndependentInstancesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetBuffTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        return false;
    }
    AActor* HostActor = SpawnBuffTestHost(World);
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

    const int32 FirstId = BuffComponent->ApplyBuff(ShortSpec).GetValue();
    const int32 SecondId = BuffComponent->ApplyBuff(LongSpec).GetValue();
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
    TestTrue(TEXT("单实例移除成功"), BuffComponent->RemoveBuff(PoisonTag, false).IsSuccess());
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

    TestTrue(TEXT("首实例施加成功"), BuffComponent->ApplyBuff(CappedSpec).IsSuccess());
    TestTrue(TEXT("第二实例施加成功"), BuffComponent->ApplyBuff(CappedSpec).IsSuccess());
    TestTrue(TEXT("超出 MaxStacks 时第三实例应被拒绝"), BuffComponent->ApplyBuff(CappedSpec).IsFailure());
    TestEqual(TEXT("独立实例总层数保持为 2"), BuffComponent->GetBuffStacks(CappedSpec.BuffTag), 2);

    HostActor->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffRemoveStacksTest,
    "Game.GameplayCore.Buff.RemoveStacks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证按层数精确削减 Buff、部分削减保持激活及归零自动清理
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
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

    TestTrue(TEXT("削减 1 层成功"), BuffComponent->RemoveBuffStacks(BleedTag, 1).IsSuccess());
    TestEqual(TEXT("削减后剩余 2 层"), BuffComponent->GetBuffStacks(BleedTag), 2);
    TestTrue(TEXT("Buff 仍存活"), BuffComponent->HasBuff(BleedTag));

    TestTrue(TEXT("削减 2 层成功"), BuffComponent->RemoveBuffStacks(BleedTag, 2).IsSuccess());
    TestEqual(TEXT("削减后剩余 0 层"), BuffComponent->GetBuffStacks(BleedTag), 0);
    TestFalse(TEXT("层数归零后 Buff 移除"), BuffComponent->HasBuff(BleedTag));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffInstanceAndQueryCoverageTest,
    "Game.GameplayCore.Buff.InstanceAndQueryCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多 Buff 实例查询、活跃 Tag 集合获取与按实例 ID 精确移除
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffInstanceAndQueryCoverageTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(GetTransientPackage());
    const FGameplayTag TagA = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Test.CoverageA")));
    const FGameplayTag TagB = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Test.CoverageB")));

    FGameBuffSpec SpecA;
    SpecA.BuffTag = TagA;
    SpecA.Duration = 10.0f;

    FGameBuffSpec SpecB;
    SpecB.BuffTag = TagB;
    SpecB.Duration = 10.0f;

    const int32 InstanceIdA = BuffComponent->ApplyBuff(SpecA).GetValue();
    const int32 InstanceIdB = BuffComponent->ApplyBuff(SpecB).GetValue();
    TestNotEqual(TEXT("实例 A 施加成功"), InstanceIdA, 0);
    TestNotEqual(TEXT("实例 B 施加成功"), InstanceIdB, 0);

    TArray<FGameplayTag> ActiveTags;
    BuffComponent->GetActiveBuffTags(ActiveTags);
    TestEqual(TEXT("活跃 Buff 标签总数为 2"), ActiveTags.Num(), 2);
    TestTrue(TEXT("包含标签 A"), ActiveTags.Contains(TagA));
    TestTrue(TEXT("包含标签 B"), ActiveTags.Contains(TagB));

    TestTrue(TEXT("根据 InstanceIdA 移除成功"), BuffComponent->RemoveBuffInstance(InstanceIdA).IsSuccess());
    TestTrue(TEXT("根据重复 InstanceIdA 移除应失败"), BuffComponent->RemoveBuffInstance(InstanceIdA).IsFailure());

    BuffComponent->GetActiveBuffTags(ActiveTags);
    TestEqual(TEXT("移除后活跃 Buff 标签数为 1"), ActiveTags.Num(), 1);
    TestFalse(TEXT("不再包含标签 A"), ActiveTags.Contains(TagA));
    TestTrue(TEXT("仍包含标签 B"), ActiveTags.Contains(TagB));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffPermanentDurationTransitionTest,
    "Game.GameplayCore.Buff.PermanentDurationTransition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证限时 Buff 通过后续叠加永久规格转换为永久 Buff
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffPermanentDurationTransitionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(GetTransientPackage());
    FGameBuffSpec Spec;
    Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.PermanentDurationTransition.Test")));
    Spec.StackPolicy = EGameBuffStackPolicy::AddDuration;
    Spec.Duration = 5.0f;
    BuffComponent->ApplyBuff(Spec);
    TestEqual(TEXT("初始限时 Buff 剩余时间正确"), BuffComponent->GetBuffRemainingTime(Spec.BuffTag), 5.0f);

    Spec.Duration = 0.0f;
    BuffComponent->ApplyBuff(Spec);
    TestEqual(TEXT("AddDuration 转永久后剩余时间归零"), BuffComponent->GetBuffRemainingTime(Spec.BuffTag), 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffDefensiveBranchesTest,
    "Game.GameplayCore.Buff.DefensiveBranches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证非法标签、零/负层数、不存在实例 ID 及非权威端写门禁与模拟端计时器自适应停用
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffDefensiveBranchesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameBuffComponent* BuffComp = NewObject<UGameBuffComponent>(GetTransientPackage());
    const FGameplayTag InvalidTag;
    const FGameplayTag ValidTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Defensive.Valid")));

    FGameBuffSpec InvalidSpec;
    TestTrue(TEXT("非法标签 ApplyBuff 被拒绝返回失败"), BuffComp->ApplyBuff(InvalidSpec).IsFailure());
    TestTrue(TEXT("非法标签 RemoveBuff 返回失败"), BuffComp->RemoveBuff(InvalidTag).IsFailure());
    TestTrue(TEXT("非法标签 RemoveBuffStacks 返回失败"), BuffComp->RemoveBuffStacks(InvalidTag, 1).IsFailure());
    TestTrue(TEXT("扣减 0 层 RemoveBuffStacks 返回失败"), BuffComp->RemoveBuffStacks(ValidTag, 0).IsFailure());
    TestTrue(TEXT("扣减负层数 RemoveBuffStacks 返回失败"), BuffComp->RemoveBuffStacks(ValidTag, -2).IsFailure());
    TestTrue(TEXT("移除不存在的实例 ID 返回失败"), BuffComp->RemoveBuffInstance(99999).IsFailure());

    UWorld* World = GetBuffTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World != nullptr) {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AActor* NonAuthActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
        TestNotNull(TEXT("非权威 Actor 创建成功"), NonAuthActor);
        if (NonAuthActor != nullptr) {
            NonAuthActor->SetRole(ROLE_SimulatedProxy);
            UGameBuffComponent* NonAuthBuffComp = NewObject<UGameBuffComponent>(NonAuthActor);
            NonAuthBuffComp->RegisterComponent();

            FGameBuffSpec ValidSpec;
            ValidSpec.BuffTag = ValidTag;
            ValidSpec.Duration = 2.0f;

            TestTrue(TEXT("非权威端拒绝 ApplyBuff"), NonAuthBuffComp->ApplyBuff(ValidSpec).IsFailure());
            TestTrue(TEXT("非权威端拒绝 RemoveBuff"), NonAuthBuffComp->RemoveBuff(ValidTag).IsFailure());
            TestTrue(TEXT("非权威端拒绝 RemoveBuffStacks"), NonAuthBuffComp->RemoveBuffStacks(ValidTag, 1).IsFailure());
            TestTrue(TEXT("非权威端拒绝 RemoveBuffInstance"), NonAuthBuffComp->RemoveBuffInstance(1).IsFailure());

            FGameBuffInstance& Inst = NonAuthBuffComp->ActiveBuffs.Items.AddDefaulted_GetRef();
            Inst.InstanceId = 1;
            Inst.Spec = ValidSpec;
            Inst.RemainingSeconds = 1.0f;
            NonAuthBuffComp->SetComponentTickEnabled(true);

            NonAuthBuffComp->TickComponent(0.4f, LEVELTICK_TimeOnly, nullptr);
            TestNearlyEqual(TEXT("非权威端本地倒计时推进"), Inst.RemainingSeconds, 0.6f, 0.001f);
            TestTrue(TEXT("仍有时长时 Tick 保持开启"), NonAuthBuffComp->IsComponentTickEnabled());

            NonAuthBuffComp->TickComponent(0.7f, LEVELTICK_TimeOnly, nullptr);
            TestEqual(TEXT("本地倒计时耗尽钳制到 0"), Inst.RemainingSeconds, 0.0f);
            TestFalse(TEXT("本地倒计时耗尽后非权威端自动关闭 Tick"), NonAuthBuffComp->IsComponentTickEnabled());

            NonAuthActor->Destroy();
        }
    }

    return true;
}

#endif
