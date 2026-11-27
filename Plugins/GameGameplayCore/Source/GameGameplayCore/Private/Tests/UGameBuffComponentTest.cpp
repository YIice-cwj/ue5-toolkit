/**
 * @file UGameBuffComponentTest.cpp
 * @brief UGameBuffComponent 核心生命周期、叠层策略、状态标签、蓝图门面、错误码与权限门禁自动化测试
 */

#include "CoreMinimal.h"
#include "Buff/UGameBuffComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Stat/UGameStatComponent.h"
#include "Stat/UGameVitalComponent.h"
#include "Tests/GameGameplayCoreTestUtils.h"
#include "Tests/UGameGameplayCoreTestReceiver.h"

#include <limits>

#if WITH_AUTOMATION_TESTS

using Game::GameplayCore::Test::FScopedTestHostActor;

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
    FScopedTestHostActor Host;
    TestTrue(TEXT("测试宿主 Actor 有效"), Host.IsValidHost());
    if (!Host.IsValidHost()) {
        return false;
    }

    UGameStatComponent* StatComponent = NewObject<UGameStatComponent>(Host.Get());
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(Host.Get());
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

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffCooldownStatusTest,
    "Game.GameplayCore.Buff.CooldownAsStatus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证以 Buff 承载技能冷却、持续期间授予状态标签（含零拷贝 HasGrantedTag 层级查询）并在冷却结束后撤销
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffCooldownStatusTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestHostActor Host;
    TestTrue(TEXT("测试宿主 Actor 有效"), Host.IsValidHost());
    if (!Host.IsValidHost()) {
        return false;
    }

    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(Host.Get());
    BuffComponent->RegisterComponent();

    const FGameplayTag CooldownParentTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Cooldown")));
    const FGameplayTag CooldownStateTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("State.Cooldown.TestSkill")));
    const FGameplayTag EmptyTag;

    FGameBuffSpec CooldownSpec;
    CooldownSpec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Cooldown.TestSkill")));
    CooldownSpec.Duration = 1.0f;
    CooldownSpec.GrantedTags.AddTag(CooldownStateTag);

    const TGameStatusResult<int32> InstanceResult = BuffComponent->ApplyBuff(CooldownSpec);
    TestTrue(TEXT("冷却 Buff 施加成功"), InstanceResult.IsSuccess());

    TestTrue(TEXT("零拷贝 HasGrantedTag 精确命中冷却状态"), BuffComponent->HasGrantedTag(CooldownStateTag));
    TestTrue(TEXT("零拷贝 HasGrantedTag 支持父标签层级匹配"), BuffComponent->HasGrantedTag(CooldownParentTag));
    TestFalse(TEXT("零拷贝 HasGrantedTag 拒绝无效标签"), BuffComponent->HasGrantedTag(EmptyTag));

    FGameplayTagContainer GrantedTags;
    BuffComponent->GetGrantedTags(GrantedTags);
    TestTrue(TEXT("冷却期间聚合状态标签生效"), GrantedTags.HasTag(CooldownStateTag));
    TestEqual(TEXT("冷却剩余时长读取正确"), BuffComponent->GetBuffRemainingTime(CooldownSpec.BuffTag), 1.0f);

    BuffComponent->TickComponent(0.5f, LEVELTICK_TimeOnly, nullptr);
    TestTrue(TEXT("冷却未结束时状态标签保持"), BuffComponent->HasBuff(CooldownSpec.BuffTag));

    BuffComponent->TickComponent(0.6f, LEVELTICK_TimeOnly, nullptr);
    TestFalse(TEXT("冷却结束时 Buff 移除"), BuffComponent->HasBuff(CooldownSpec.BuffTag));
    TestFalse(TEXT("冷却结束后零拷贝 HasGrantedTag 返回 false"), BuffComponent->HasGrantedTag(CooldownStateTag));

    BuffComponent->GetGrantedTags(GrantedTags);
    TestFalse(TEXT("冷却结束后状态标签撤销"), GrantedTags.HasTag(CooldownStateTag));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffIndependentInstancesTest,
    "Game.GameplayCore.Buff.IndependentInstances",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证独立实例策略下各 Buff 实例独立计时、跨策略混叠聚合广播及 MaxStacks 堆叠上限错误码拦截
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffIndependentInstancesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestHostActor Host;
    TestTrue(TEXT("测试宿主 Actor 有效"), Host.IsValidHost());
    if (!Host.IsValidHost()) {
        return false;
    }

    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(Host.Get());
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

    FGameBuffSpec RefreshMixedSpec = ShortSpec;
    RefreshMixedSpec.StackPolicy = EGameBuffStackPolicy::RefreshDuration;
    TestTrue(TEXT("混叠非独立策略成功"), BuffComponent->ApplyBuff(RefreshMixedSpec).IsSuccess());
    TestEqual(TEXT("混叠叠加后广播聚合总层数为 3 而非单实例层数"), LastChangedStacks, 3);
    TestEqual(TEXT("混叠后总层数为 3"), BuffComponent->GetBuffStacks(PoisonTag), 3);

    TestTrue(TEXT("移除首实例后剩余 1 层"), BuffComponent->RemoveBuffInstance(FirstId).IsSuccess());
    TestEqual(TEXT("剩余层数为 1"), BuffComponent->GetBuffStacks(PoisonTag), 1);

    BuffComponent->ApplyBuff(ShortSpec);
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
    const TGameStatusResult<int32> OverCapRes = BuffComponent->ApplyBuff(CappedSpec);
    TestTrue(TEXT("超出 MaxStacks 时第三实例应被拒绝"), OverCapRes.IsFailure());
    TestEqual(TEXT("超限错误归属 Gameplay 模块"), OverCapRes.GetError().ModuleId, Game::ModuleId::Gameplay);
    TestEqual(TEXT("超限错误码为 MaxBuffStacksReached"), OverCapRes.GetError().ErrorCode, Game::GameplayCoreErrorCodes::MaxBuffStacksReached);
    TestEqual(TEXT("独立实例总层数保持为 2"), BuffComponent->GetBuffStacks(CappedSpec.BuffTag), 2);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffRemoveStacksTest,
    "Game.GameplayCore.Buff.RemoveStacks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证按层数精确削减 Buff、绑定数值组件后修正器原子重缩放、跨独立实例扣层及归零清理
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffRemoveStacksTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* StatComp = NewObject<UGameStatComponent>(GetTransientPackage());
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(GetTransientPackage());
    BuffComponent->BindStatComponent(StatComp);

    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Test.BleedAtk")));
    const FGameplayTag BleedTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Test.Bleed")));
    StatComp->RegisterStat(AttackTag, 100.0f, 1000.0f, 0.0f);

    FGameBuffSpec Spec;
    Spec.BuffTag = BleedTag;
    Spec.Duration = 10.0f;
    Spec.MaxStacks = 5;
    Spec.StackPolicy = EGameBuffStackPolicy::RefreshDuration;
    FGameStatModifierSpec AtkMod;
    AtkMod.StatTag = AttackTag;
    AtkMod.Modifier = FGameStatModifier(10.0f, EGameStatModifierOp::Add);
    Spec.Modifiers.Add(AtkMod);

    BuffComponent->ApplyBuff(Spec);
    BuffComponent->ApplyBuff(Spec);
    BuffComponent->ApplyBuff(Spec);
    TestEqual(TEXT("三层初始生效"), BuffComponent->GetBuffStacks(BleedTag), 3);
    TestEqual(TEXT("三层攻击修正后有效值为 130"), StatComp->GetStatEffectiveCurrent(AttackTag), 130.0f);

    TestTrue(TEXT("削减 1 层成功"), BuffComponent->RemoveBuffStacks(BleedTag, 1).IsSuccess());
    TestEqual(TEXT("削减后剩余 2 层"), BuffComponent->GetBuffStacks(BleedTag), 2);
    TestEqual(TEXT("部分扣层后修正器精确重缩放至 120"), StatComp->GetStatEffectiveCurrent(AttackTag), 120.0f);
    TestTrue(TEXT("Buff 仍存活"), BuffComponent->HasBuff(BleedTag));

    TestTrue(TEXT("削减 2 层成功"), BuffComponent->RemoveBuffStacks(BleedTag, 2).IsSuccess());
    TestEqual(TEXT("削减后剩余 0 层"), BuffComponent->GetBuffStacks(BleedTag), 0);
    TestEqual(TEXT("层数归零后修正器完全回滚至 100"), StatComp->GetStatEffectiveCurrent(AttackTag), 100.0f);
    TestFalse(TEXT("层数归零后 Buff 移除"), BuffComponent->HasBuff(BleedTag));

    const FGameplayTag MultiTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Test.MultiStackRemove")));
    FGameBuffSpec IndepSpec;
    IndepSpec.BuffTag = MultiTag;
    IndepSpec.Duration = 10.0f;
    IndepSpec.MaxStacks = 5;
    IndepSpec.StackPolicy = EGameBuffStackPolicy::Independent;
    IndepSpec.Modifiers.Add(AtkMod);
    BuffComponent->ApplyBuff(IndepSpec);
    BuffComponent->ApplyBuff(IndepSpec);
    BuffComponent->ApplyBuff(IndepSpec);
    TestEqual(TEXT("三个独立实例总修正为 130"), StatComp->GetStatEffectiveCurrent(AttackTag), 130.0f);

    TestTrue(TEXT("跨独立实例扣减 2 层成功"), BuffComponent->RemoveBuffStacks(MultiTag, 2).IsSuccess());
    TestEqual(TEXT("剩余 1 个独立实例"), BuffComponent->GetBuffStacks(MultiTag), 1);
    TestEqual(TEXT("跨实例扣层后有效攻击回落至 110"), StatComp->GetStatEffectiveCurrent(AttackTag), 110.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffInstanceAndQueryCoverageTest,
    "Game.GameplayCore.Buff.InstanceAndQueryCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多 Buff 实例查询、ClearAllBuffs 批量清理、蓝图 K2 门面与动态多播委托完整覆盖
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffInstanceAndQueryCoverageTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* StatComp = NewObject<UGameStatComponent>(GetTransientPackage());
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(GetTransientPackage());
    UGameGameplayCoreTestReceiver* Receiver = NewObject<UGameGameplayCoreTestReceiver>();
    BuffComponent->BindStatComponent(StatComp);
    BuffComponent->OnBuffChanged.AddDynamic(Receiver, &UGameGameplayCoreTestReceiver::HandleBuffChanged);
    BuffComponent->OnBuffRemoved.AddDynamic(Receiver, &UGameGameplayCoreTestReceiver::HandleBuffRemoved);

    const FGameplayTag AtkTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Test.ClearAllAtk")));
    const FGameplayTag TagA = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Test.CoverageA")));
    const FGameplayTag TagB = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Test.CoverageB")));
    StatComp->RegisterStat(AtkTag, 100.0f, 1000.0f, 0.0f);

    FGameBuffSpec SpecA;
    SpecA.BuffTag = TagA;
    SpecA.Duration = 10.0f;
    SpecA.MaxStacks = 3;
    FGameStatModifierSpec AtkMod;
    AtkMod.StatTag = AtkTag;
    AtkMod.Modifier = FGameStatModifier(15.0f, EGameStatModifierOp::Add);
    SpecA.Modifiers.Add(AtkMod);

    FGameBuffSpec SpecB;
    SpecB.BuffTag = TagB;
    SpecB.Duration = 10.0f;
    SpecB.Modifiers.Add(AtkMod);

    const int32 InstanceIdA = BuffComponent->K2_ApplyBuff(SpecA);
    const int32 InstanceIdB = BuffComponent->K2_ApplyBuff(SpecB);
    TestTrue(TEXT("K2_ApplyBuff 实例 A 施加成功"), InstanceIdA > 0);
    TestTrue(TEXT("K2_ApplyBuff 实例 B 施加成功"), InstanceIdB > 0);
    TestEqual(TEXT("动态多播委托收到 2 次变更"), Receiver->BuffChangedCount, 2);
    TestEqual(TEXT("动态多播委托最新标签为 TagB"), Receiver->LastBuffChangedTag, TagB);

    TArray<FGameplayTag> ActiveTags;
    BuffComponent->GetActiveBuffTags(ActiveTags);
    TestEqual(TEXT("活跃 Buff 标签总数为 2"), ActiveTags.Num(), 2);
    TestTrue(TEXT("包含标签 A"), ActiveTags.Contains(TagA));
    TestTrue(TEXT("包含标签 B"), ActiveTags.Contains(TagB));

    TestTrue(TEXT("K2_RemoveBuffInstance 移除 A 成功"), BuffComponent->K2_RemoveBuffInstance(InstanceIdA));
    TestFalse(TEXT("重复 K2_RemoveBuffInstance 失败"), BuffComponent->K2_RemoveBuffInstance(InstanceIdA));
    TestEqual(TEXT("动态多播委托收到 1 次移除"), Receiver->BuffRemovedCount, 1);
    TestEqual(TEXT("动态多播委托移除标签为 TagA"), Receiver->LastBuffRemovedTag, TagA);

    BuffComponent->K2_ApplyBuff(SpecA);
    BuffComponent->K2_ApplyBuff(SpecA);
    TestTrue(TEXT("K2_RemoveBuffStacks 扣减 1 层成功"), BuffComponent->K2_RemoveBuffStacks(TagA, 1));
    TestEqual(TEXT("TagA 剩余 1 层"), BuffComponent->GetBuffStacks(TagA), 1);

    TestEqual(TEXT("双 Buff 激活时攻击为 130"), StatComp->GetStatEffectiveCurrent(AtkTag), 130.0f);
    const int32 ClearedCount = BuffComponent->K2_ClearAllBuffs();
    TestEqual(TEXT("K2_ClearAllBuffs 清除 2 个实例"), ClearedCount, 2);
    TestEqual(TEXT("批量清空后攻击修正完全回滚至 100"), StatComp->GetStatEffectiveCurrent(AtkTag), 100.0f);
    TestEqual(TEXT("空容器再调 ClearAllBuffs 返回 0"), BuffComponent->ClearAllBuffs().GetValue(), 0);
    TestFalse(TEXT("K2_RemoveBuff 对空容器返回 false"), BuffComponent->K2_RemoveBuff(TagA));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffPermanentDurationTransitionTest,
    "Game.GameplayCore.Buff.PermanentDurationTransition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 AddDuration 策略下正向持续时间累加，以及后续叠加永久规格转换为永久 Buff
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffPermanentDurationTransitionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameBuffComponent* BuffComponent = NewObject<UGameBuffComponent>(GetTransientPackage());
    FGameBuffSpec Spec;
    Spec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.PermanentDurationTransition.Test")));
    Spec.StackPolicy = EGameBuffStackPolicy::AddDuration;
    Spec.MaxStacks = 3;
    Spec.Duration = 5.0f;

    BuffComponent->ApplyBuff(Spec);
    TestEqual(TEXT("初始限时 Buff 剩余时间正确"), BuffComponent->GetBuffRemainingTime(Spec.BuffTag), 5.0f);

    BuffComponent->ApplyBuff(Spec);
    TestNearlyEqual(TEXT("AddDuration 正向叠加持续时间累加为 10 秒"), BuffComponent->GetBuffRemainingTime(Spec.BuffTag), 10.0f, 0.001f);
    TestEqual(TEXT("AddDuration 叠层数增至 2"), BuffComponent->GetBuffStacks(Spec.BuffTag), 2);

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
 * @brief 验证非法标签、非有限时长、零/负层数、非法/不存在实例 ID、精确错误码及非权威端写门禁与本地倒计时
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffDefensiveBranchesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameBuffComponent* BuffComp = NewObject<UGameBuffComponent>(GetTransientPackage());
    const FGameplayTag InvalidTag;
    const FGameplayTag ValidTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Defensive.Valid")));

    FGameBuffSpec InvalidSpec;
    const TGameStatusResult<int32> InvalidTagRes = BuffComp->ApplyBuff(InvalidSpec);
    TestTrue(TEXT("非法标签 ApplyBuff 被拒绝返回失败"), InvalidTagRes.IsFailure());
    TestEqual(TEXT("非法标签错误码为 InvalidParameter"), InvalidTagRes.GetError().ErrorCode, Game::GameplayCoreErrorCodes::InvalidParameter);
    TestEqual(TEXT("非法标签模块 ID 为 Gameplay"), InvalidTagRes.GetError().ModuleId, Game::ModuleId::Gameplay);

    FGameBuffSpec NaNDurationSpec;
    NaNDurationSpec.BuffTag = ValidTag;
    NaNDurationSpec.Duration = std::numeric_limits<float>::quiet_NaN();
    const TGameStatusResult<int32> NaNDurationRes = BuffComp->ApplyBuff(NaNDurationSpec);
    TestTrue(TEXT("NaN 持续时间 ApplyBuff 被拒绝"), NaNDurationRes.IsFailure());
    TestEqual(TEXT("NaN 时长错误码为 InvalidParameter"), NaNDurationRes.GetError().ErrorCode, Game::GameplayCoreErrorCodes::InvalidParameter);

    TestTrue(TEXT("非法标签 RemoveBuff 返回失败"), BuffComp->RemoveBuff(InvalidTag).IsFailure());
    TestTrue(TEXT("非法标签 RemoveBuffStacks 返回失败"), BuffComp->RemoveBuffStacks(InvalidTag, 1).IsFailure());
    TestTrue(TEXT("扣减 0 层 RemoveBuffStacks 返回失败"), BuffComp->RemoveBuffStacks(ValidTag, 0).IsFailure());
    TestTrue(TEXT("扣减负层数 RemoveBuffStacks 返回失败"), BuffComp->RemoveBuffStacks(ValidTag, -2).IsFailure());

    const TGameStatusResult<void> ZeroIdRes = BuffComp->RemoveBuffInstance(0);
    TestTrue(TEXT("移除 0 实例 ID 返回失败"), ZeroIdRes.IsFailure());
    TestEqual(TEXT("移除 0 实例 ID 错误码为 InvalidParameter"), ZeroIdRes.GetError().ErrorCode, Game::GameplayCoreErrorCodes::InvalidParameter);

    const TGameStatusResult<void> MissingIdRes = BuffComp->RemoveBuffInstance(99999);
    TestTrue(TEXT("移除不存在的实例 ID 返回失败"), MissingIdRes.IsFailure());
    TestEqual(TEXT("不存在实例 ID 错误码为 BuffNotFound"), MissingIdRes.GetError().ErrorCode, Game::GameplayCoreErrorCodes::BuffNotFound);

    FScopedTestHostActor NonAuthHost;
    TestTrue(TEXT("非权威测试宿主有效"), NonAuthHost.IsValidHost());
    if (NonAuthHost.IsValidHost()) {
        NonAuthHost.Get()->SetRole(ROLE_SimulatedProxy);
        UGameBuffComponent* NonAuthBuffComp = NewObject<UGameBuffComponent>(NonAuthHost.Get());
        NonAuthBuffComp->RegisterComponent();
        TestFalse(TEXT("非权威端默认禁止写入"), NonAuthBuffComp->IsAuthoritativeWriteAllowed());

        FGameBuffSpec ValidSpec;
        ValidSpec.BuffTag = ValidTag;
        ValidSpec.Duration = 2.0f;

        const TGameStatusResult<int32> NonAuthApply = NonAuthBuffComp->ApplyBuff(ValidSpec);
        TestTrue(TEXT("非权威端拒绝 ApplyBuff"), NonAuthApply.IsFailure());
        TestEqual(TEXT("非权威端 ApplyBuff 错误码为 NotAuthority"), NonAuthApply.GetError().ErrorCode, Game::GameplayCoreErrorCodes::NotAuthority);
        TestTrue(TEXT("非权威端拒绝 RemoveBuff"), NonAuthBuffComp->RemoveBuff(ValidTag).IsFailure());
        TestTrue(TEXT("非权威端拒绝 RemoveBuffStacks"), NonAuthBuffComp->RemoveBuffStacks(ValidTag, 1).IsFailure());
        TestTrue(TEXT("非权威端拒绝 RemoveBuffInstance"), NonAuthBuffComp->RemoveBuffInstance(1).IsFailure());
        TestTrue(TEXT("非权威端拒绝 ClearAllBuffs"), NonAuthBuffComp->ClearAllBuffs().IsFailure());

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
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameBuffReentrancyAndUnbindTest,
    "Game.GameplayCore.Buff.ReentrancyAndUnbind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Buff 施加/移除/清空期间由属性变更触发的重入回调安全（防 UAF/越界/抹除）及 BindStatComponent(nullptr) 显式解绑隔离
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameBuffReentrancyAndUnbindTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestHostActor Host;
    TestTrue(TEXT("宿主 Actor 有效"), Host.IsValidHost());
    if (!Host.IsValidHost()) {
        return false;
    }

    UGameStatComponent* StatComp = NewObject<UGameStatComponent>(Host.Get());
    UGameVitalComponent* VitalComp = NewObject<UGameVitalComponent>(Host.Get());
    UGameBuffComponent* BuffComp = NewObject<UGameBuffComponent>(Host.Get());
    Host.Get()->AddInstanceComponent(StatComp);
    Host.Get()->AddInstanceComponent(VitalComp);
    Host.Get()->AddInstanceComponent(BuffComp);
    StatComp->RegisterComponent();
    VitalComp->RegisterComponent();
    BuffComp->RegisterComponent();

    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.BuffReentrancy.Health")));
    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.BuffReentrancy.Attack")));
    const FGameplayTag CurseTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Reentrancy.Curse")));
    const FGameplayTag LifeGuardTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Reentrancy.LifeGuard")));
    const FGameplayTag DeathBuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Reentrancy.Dead")));

    StatComp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);
    StatComp->RegisterStat(AttackTag, 50.0f, 500.0f, 0.0f);
    VitalComp->BindStatComponent(StatComp);
    VitalComp->SetPrimaryVitalTag(HealthTag);
    BuffComp->BindStatComponent(StatComp);

    FGameBuffSpec DeathSpec;
    DeathSpec.BuffTag = DeathBuffTag;
    DeathSpec.Duration = 0.0f;
    FGameStatModifierSpec DeathAtkMod;
    DeathAtkMod.StatTag = AttackTag;
    DeathAtkMod.Modifier = FGameStatModifier(10.0f, EGameStatModifierOp::Add);
    DeathSpec.Modifiers.Add(DeathAtkMod);

    FGameBuffSpec CurseSpec;
    CurseSpec.BuffTag = CurseTag;
    CurseSpec.Duration = 5.0f;
    CurseSpec.MaxStacks = 3;
    FGameStatModifierSpec LethalHealthMod;
    LethalHealthMod.StatTag = HealthTag;
    LethalHealthMod.Modifier = FGameStatModifier(-200.0f, EGameStatModifierOp::Add);
    CurseSpec.Modifiers.Add(LethalHealthMod);

    bool bReenterOnDeathWithApply = false;
    bool bReenterOnDeathWithClearAndApply = false;
    bool bSawOldBuffDuringClear = true;
    int32 DeathTriggeredCount = 0;

    VitalComp->OnVitalStateChangedNative.AddLambda(
        [BuffComp, LifeGuardTag, DeathSpec, &bReenterOnDeathWithApply, &bReenterOnDeathWithClearAndApply, &bSawOldBuffDuringClear, &DeathTriggeredCount](
            bool bIsAlive,
            const FGameplayTag&
        ) {
            if (bIsAlive) {
                return;
            }
            ++DeathTriggeredCount;
            if (bReenterOnDeathWithApply) {
                bReenterOnDeathWithApply = false;
                for (int32 Index = 0; Index < 16; ++Index) {
                    FGameBuffSpec Filler;
                    Filler.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(
                        FName(*FString::Printf(TEXT("Buff.Reentrancy.Filler.%d"), Index)));
                    BuffComp->ApplyBuff(Filler);
                }
                BuffComp->ClearAllBuffs();
            } else if (bReenterOnDeathWithClearAndApply) {
                bReenterOnDeathWithClearAndApply = false;
                bSawOldBuffDuringClear = BuffComp->HasBuff(LifeGuardTag);
                BuffComp->ApplyBuff(DeathSpec);
            }
        }
    );

    bReenterOnDeathWithApply = true;
    const TGameStatusResult<int32> CurseRes = BuffComp->ApplyBuff(CurseSpec);
    TestTrue(TEXT("致死诅咒 Buff 施加期间重入扩容并清空全部 Buff 不发生悬垂引用崩溃"), CurseRes.IsSuccess());
    TestEqual(TEXT("触发了一次致死回调"), DeathTriggeredCount, 1);
    TestFalse(TEXT("回调清空后诅咒 Buff 已移除"), BuffComp->HasBuff(CurseTag));
    TestEqual(TEXT("回调清空后致死诅咒修正器完整回滚，生命恢复 100"), StatComp->GetStatEffectiveCurrent(HealthTag), 100.0f);
    TestTrue(TEXT("回滚后恢复存活"), VitalComp->IsVitalAlive());

    StatComp->SetStatCurrent(HealthTag, 0.0f);
    TestFalse(TEXT("基础生命归零进入阵亡态"), VitalComp->IsVitalAlive());

    FGameBuffSpec LifeGuardSpec;
    LifeGuardSpec.BuffTag = LifeGuardTag;
    LifeGuardSpec.Duration = 10.0f;
    FGameStatModifierSpec GuardMod;
    GuardMod.StatTag = HealthTag;
    GuardMod.Modifier = FGameStatModifier(50.0f, EGameStatModifierOp::Add);
    LifeGuardSpec.Modifiers.Add(GuardMod);

    const int32 GuardId = BuffComp->ApplyBuff(LifeGuardSpec).GetValue();
    TestTrue(TEXT("保命 Buff 生效后复活"), VitalComp->IsVitalAlive());

    bReenterOnDeathWithClearAndApply = true;
    TestTrue(TEXT("ClearAllBuffs 期间重入施加 DeathBuff 成功"), BuffComp->ClearAllBuffs().IsSuccess());
    TestFalse(TEXT("ClearAllBuffs 触发死亡回调时旧保命 Buff 已从容器移除，无状态撕裂"), bSawOldBuffDuringClear);
    TestTrue(TEXT("重入施加的 DeathBuff 未被 ClearAllBuffs 后续清空抹除"), BuffComp->HasBuff(DeathBuffTag));
    TestEqual(TEXT("重入施加的 DeathBuff 修正器完整生效"), StatComp->GetStatEffectiveCurrent(AttackTag), 60.0f);

    const int32 SecondGuardId = BuffComp->ApplyBuff(LifeGuardSpec).GetValue();
    TestTrue(TEXT("再次施加保命 Buff 成功"), SecondGuardId > GuardId && VitalComp->IsVitalAlive());
    bReenterOnDeathWithApply = true;
    TestTrue(TEXT("RemoveBuffInstance 触发死亡回调并重入 ClearAllBuffs 不越界崩溃"), BuffComp->RemoveBuffInstance(SecondGuardId).IsSuccess());
    TestFalse(TEXT("重入清空后容器为空"), BuffComp->HasBuff(LifeGuardTag));

    BuffComp->ClearAllBuffs();
    StatComp->SetStatCurrent(HealthTag, 100.0f);
    BuffComp->BindStatComponent(nullptr);

    FGameBuffSpec UnboundAtkSpec;
    UnboundAtkSpec.BuffTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Buff.Reentrancy.UnboundAtk")));
    UnboundAtkSpec.Modifiers.Add(DeathAtkMod);
    TestTrue(TEXT("显式解绑后仍可正常施加 Buff"), BuffComp->ApplyBuff(UnboundAtkSpec).IsSuccess());
    TestEqual(TEXT("显式 BindStatComponent(nullptr) 后不再偷偷重绑宿主 StatComp"), StatComp->GetStatEffectiveCurrent(AttackTag), 50.0f);

    BuffComp->BindStatComponent(StatComp);
    TestEqual(TEXT("重新显式绑定后既有 Buff 修正器立即迁移生效"), StatComp->GetStatEffectiveCurrent(AttackTag), 60.0f);

    BuffComp->ClearAllBuffs();
    StatComp->SetStatCurrent(HealthTag, 0.0f);
    UGameStatComponent* TargetStatComp = NewObject<UGameStatComponent>(Host.Get());
    TargetStatComp->RegisterComponent();
    TargetStatComp->RegisterStat(HealthTag, 0.0f, 100.0f, 0.0f);
    TargetStatComp->RegisterStat(AttackTag, 50.0f, 500.0f, 0.0f);

    BuffComp->ApplyBuff(LifeGuardSpec);
    TestTrue(TEXT("切换目标组件前原 StatComp 保命生效"), VitalComp->IsVitalAlive());
    bReenterOnDeathWithClearAndApply = true;
    BuffComp->BindStatComponent(TargetStatComp);
    TestEqual(TEXT("BindStatComponent 切换期死亡回调施加的 DeathBuff 不泄漏回旧 StatComp"), StatComp->GetStatEffectiveCurrent(AttackTag), 50.0f);
    TestEqual(TEXT("BindStatComponent 切换后新 StatComp 精确接收 DeathBuff 修正且零重复挂接"), TargetStatComp->GetStatEffectiveCurrent(AttackTag), 60.0f);
    TestEqual(TEXT("BindStatComponent 切换后新 StatComp 精确接收原保命 Buff 修正"), TargetStatComp->GetStatEffectiveCurrent(HealthTag), 50.0f);

    return true;
}

#endif
