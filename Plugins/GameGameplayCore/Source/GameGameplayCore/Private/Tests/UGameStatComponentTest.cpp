#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Damage/FGameDamageContext.h"
#include "Damage/FGameDamageResult.h"
#include "Stat/UGameStatComponent.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDamageResultTest,
    "Game.GameplayCore.Damage.ResultFeedback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameDamageResultTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameDamageResult Result;
    Result.SetActualDamage(75.0f);

    const FGameplayTag HitTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.Hit")));
    const FGameplayTag FatalTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.Fatal")));
    const FGameplayTag CriticalTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.Critical")));
    const FGameplayTag StaggerTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.Stagger")));
    const FGameplayTag HitStopTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.HitStop")));

    Result.AddFeedbackTag(HitTag);
    TestTrue(TEXT("Hit tag should exist"), Result.HasFeedbackTag(HitTag));
    TestFalse(TEXT("Fatal tag should not exist"), Result.HasFeedbackTag(FatalTag));

    Result.AddFeedbackTag(CriticalTag);
    TestTrue(TEXT("Feedback tag should exist"), Result.HasFeedbackTag(CriticalTag));
    TestFalse(TEXT("Unadded tag should not exist"), Result.HasFeedbackTag(StaggerTag));

    Result.SetDynamicValue(HitStopTag, 0.12f);
    TestEqual(TEXT("Dynamic value should match"), Result.GetDynamicValue(HitStopTag), 0.12f);
    TestEqual(TEXT("Missing dynamic value should return default"), Result.GetDynamicValue(StaggerTag, 5.0f), 5.0f);

    Result.RemoveFeedbackTag(CriticalTag);
    TestFalse(TEXT("Feedback tag should be removed"), Result.HasFeedbackTag(CriticalTag));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatMultiRegistrationTest,
    "Game.GameplayCore.Stat.RegistrationAndClamping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameStatMultiRegistrationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();

    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Health")));
    const FGameplayTag ShieldTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Shield")));

    Comp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);
    Comp->RegisterStat(ShieldTag, 50.0f, 50.0f, 0.0f);

    TestTrue(TEXT("Health stat should exist"), Comp->HasStat(HealthTag));
    TestTrue(TEXT("Shield stat should exist"), Comp->HasStat(ShieldTag));
    TestEqual(TEXT("Health current should be 100"), Comp->GetStatCurrent(HealthTag), 100.0f);
    TestEqual(TEXT("Shield current should be 50"), Comp->GetStatCurrent(ShieldTag), 50.0f);

    float ActualDelta = 0.0f;
    const bool bModified = Comp->ModifyStat(ShieldTag, -30.0f, ActualDelta);
    TestTrue(TEXT("Shield deduction should succeed"), bModified);
    TestEqual(TEXT("Shield actual delta should be -30"), ActualDelta, -30.0f);
    TestEqual(TEXT("Shield remaining should be 20"), Comp->GetStatCurrent(ShieldTag), 20.0f);
    TestEqual(TEXT("Shield percent should be 0.4"), Comp->GetStatPercent(ShieldTag), 0.4f);

    Comp->ModifyStat(ShieldTag, -50.0f, ActualDelta);
    TestEqual(TEXT("Shield clamp delta should be -20"), ActualDelta, -20.0f);
    TestEqual(TEXT("Shield should clamp to min 0"), Comp->GetStatCurrent(ShieldTag), 0.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatBoundaryClampingTest,
    "Game.GameplayCore.Stat.BoundaryClamping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameStatBoundaryClampingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Health.ClampTest")));

    Comp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);
    TestEqual(TEXT("初始属性值正确"), Comp->GetStatCurrent(HealthTag), 100.0f);

    float ActualDelta = 0.0f;
    Comp->ModifyStat(HealthTag, -150.0f, ActualDelta);
    TestEqual(TEXT("数值扣减超出下限时应 Clamp 到 Min"), Comp->GetStatCurrent(HealthTag), 0.0f);
    TestEqual(TEXT("实际增量应为 -100"), ActualDelta, -100.0f);

    Comp->ModifyStat(HealthTag, 200.0f, ActualDelta);
    TestEqual(TEXT("数值增加超出上限时应 Clamp 到 Max"), Comp->GetStatCurrent(HealthTag), 100.0f);
    TestEqual(TEXT("实际增量应为 100"), ActualDelta, 100.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatProxyTest,
    "Game.GameplayCore.Stat.ProxyMode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameStatProxyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag EnergyTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Energy")));

    Comp->RegisterStat(EnergyTag, 10.0f, 10.0f, 0.0f);

    float ExternalCurrent = 42.0f;
    float ExternalMax = 150.0f;

    Comp->BindNativeStatQueries(
        FGameStatQueryDelegate::CreateLambda([&ExternalCurrent](const FGameplayTag&) { return ExternalCurrent; }),
        FGameStatQueryDelegate::CreateLambda([&ExternalMax](const FGameplayTag&) { return ExternalMax; })
    );

    TestEqual(TEXT("Proxy current value"), Comp->GetStatCurrent(EnergyTag), 42.0f);
    TestEqual(TEXT("Proxy max value"), Comp->GetStatMax(EnergyTag), 150.0f);

    Comp->UnbindStatSource();
    TestEqual(TEXT("Standalone fallback current should cache proxy value"), Comp->GetStatCurrent(EnergyTag), 42.0f);
    TestEqual(TEXT("Standalone fallback max should cache proxy value"), Comp->GetStatMax(EnergyTag), 150.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatProxyWriteContractTest,
    "Game.GameplayCore.Stat.ProxyWriteContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameStatProxyWriteContractTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag EnergyTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Energy.ProxyWrite")));

    Comp->RegisterStat(EnergyTag, 40.0f, 100.0f, 0.0f);

    float ExternalCurrent = 40.0f;
    float ExternalMax = 100.0f;
    Comp->BindNativeStatQueries(
        FGameStatQueryDelegate::CreateLambda([&ExternalCurrent](const FGameplayTag&) { return ExternalCurrent; }),
        FGameStatQueryDelegate::CreateLambda([&ExternalMax](const FGameplayTag&) { return ExternalMax; })
    );

    float ActualDelta = -1.0f;
    TestFalse(TEXT("未绑定写委托时代理写被拒绝"), Comp->ModifyStat(EnergyTag, -5.0f, ActualDelta));
    TestEqual(TEXT("拒绝写入时实际变动为零"), ActualDelta, 0.0f);
    TestFalse(TEXT("未绑定写委托时 CanWriteStat 为 false"), Comp->CanWriteStat());

    Comp->BindNativeStatWriter(FGameStatWriteDelegate::CreateLambda(
        [&ExternalCurrent, &ExternalMax](const FGameplayTag&, EGameStatWriteOp Op, float Value, float& OutApplied) {
            if (Op == EGameStatWriteOp::Modify) {
                const float Previous = ExternalCurrent;
                ExternalCurrent = FMath::Clamp(ExternalCurrent + Value, 0.0f, ExternalMax);
                OutApplied = ExternalCurrent - Previous;
                return true;
            }
            if (Op == EGameStatWriteOp::SetCurrent) {
                ExternalCurrent = FMath::Clamp(Value, 0.0f, ExternalMax);
                OutApplied = 0.0f;
                return true;
            }
            if (Op == EGameStatWriteOp::SetMax) {
                ExternalMax = FMath::Max(Value, 0.0f);
                OutApplied = 0.0f;
                return true;
            }
            return false;
        }
    ));

    TestTrue(TEXT("绑定写委托后 CanWriteStat 为 true"), Comp->CanWriteStat());
    TestTrue(TEXT("代理增量写入成功"), Comp->ModifyStat(EnergyTag, -10.0f, ActualDelta));
    TestEqual(TEXT("代理增量实际变动正确"), ActualDelta, -10.0f);
    TestEqual(TEXT("外部源数值已更新"), ExternalCurrent, 30.0f);
    TestEqual(TEXT("代理读取回落到外部源新值"), Comp->GetStatCurrent(EnergyTag), 30.0f);

    Comp->SetStatCurrent(EnergyTag, 60.0f);
    TestEqual(TEXT("代理设置当前值透传成功"), Comp->GetStatCurrent(EnergyTag), 60.0f);

    Comp->SetStatMax(EnergyTag, 120.0f);
    TestEqual(TEXT("代理设置上限透传成功"), Comp->GetStatMax(EnergyTag), 120.0f);

    Comp->AddStatModifier(EnergyTag, FGameStatModifier(10.0f, EGameStatModifierOp::Add));
    float BroadcastOld = 0.0f;
    float BroadcastNew = 0.0f;
    Comp->SubscribeStatChanged(EnergyTag, FOnGameStatChangedNative::FDelegate::CreateLambda(
        [&BroadcastOld, &BroadcastNew](const FGameplayTag&, float OldVal, float NewVal, float) {
            BroadcastOld = OldVal;
            BroadcastNew = NewVal;
        }));

    Comp->SetStatCurrent(EnergyTag, 80.0f);
    TestEqual(TEXT("代理写且带修正器时广播派生有效旧值"), BroadcastOld, 70.0f);
    TestEqual(TEXT("代理写且带修正器时广播派生有效新值"), BroadcastNew, 90.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatTaggedSubscriptionTest,
    "Game.GameplayCore.Stat.TaggedSubscription",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameStatTaggedSubscriptionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Subscribe.Health")));
    const FGameplayTag ShieldTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Subscribe.Shield")));

    Comp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);
    Comp->RegisterStat(ShieldTag, 50.0f, 50.0f, 0.0f);

    int32 BroadcastCount = 0;
    float ObservedOld = 0.0f;
    float ObservedNew = 0.0f;
    const FDelegateHandle Handle = Comp->SubscribeStatChanged(
        HealthTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda(
            [&BroadcastCount, &ObservedOld, &ObservedNew](const FGameplayTag&, float OldValue, float NewValue, float) {
                ++BroadcastCount;
                ObservedOld = OldValue;
                ObservedNew = NewValue;
            }
        )
    );

    TestEqual(TEXT("订阅即回放当前值"), BroadcastCount, 1);
    TestEqual(TEXT("回放旧值为当前值"), ObservedOld, 100.0f);
    TestEqual(TEXT("回放新值为当前值"), ObservedNew, 100.0f);

    Comp->ModifyStat(ShieldTag, -5.0f);
    TestEqual(TEXT("非订阅属性变化不触发回调"), BroadcastCount, 1);

    Comp->ModifyStat(HealthTag, -25.0f);
    TestEqual(TEXT("订阅属性变化触发回调"), BroadcastCount, 2);
    TestEqual(TEXT("回调旧值正确"), ObservedOld, 100.0f);
    TestEqual(TEXT("回调新值正确"), ObservedNew, 75.0f);

    Comp->UnsubscribeStatChanged(HealthTag, Handle);
    Comp->ModifyStat(HealthTag, -5.0f);
    TestEqual(TEXT("取消订阅后不再触发回调"), BroadcastCount, 2);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatRegisterReplayTest,
    "Game.GameplayCore.Stat.RegisterReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameStatRegisterReplayTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag ManaTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Subscribe.LateRegister")));

    int32 CallbackCount = 0;
    float ObservedNew = 0.0f;
    Comp->SubscribeStatChanged(
        ManaTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda(
            [&CallbackCount, &ObservedNew](const FGameplayTag&, float, float NewValue, float) {
                ++CallbackCount;
                ObservedNew = NewValue;
            }
        )
    );
    TestEqual(TEXT("未注册属性订阅时不回放"), CallbackCount, 0);

    Comp->RegisterStat(ManaTag, 60.0f, 120.0f, 0.0f);
    TestEqual(TEXT("注册时向既有订阅者回放初始值"), CallbackCount, 1);
    TestEqual(TEXT("注册回放值为初始当前值"), ObservedNew, 60.0f);

    Comp->AddStatModifier(ManaTag, FGameStatModifier(20.0f, EGameStatModifierOp::Add));
    TestEqual(TEXT("注册后修正器变化继续触发"), CallbackCount, 2);
    TestEqual(TEXT("修正后回放值为派生值"), ObservedNew, 80.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatOwnerBoundSubscriptionTest,
    "Game.GameplayCore.Stat.OwnerBoundSubscription",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameStatOwnerBoundSubscriptionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag ArmorTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Subscribe.OwnerArmor")));
    Comp->RegisterStat(ArmorTag, 80.0f, 80.0f, 0.0f);

    UGameStatComponent* Owner = NewObject<UGameStatComponent>();
    int32 CallbackCount = 0;
    float LastNewValue = 0.0f;
    const FDelegateHandle Handle = Comp->SubscribeStatChanged(Owner, ArmorTag,
        [&CallbackCount, &LastNewValue](const FGameplayTag&, float, float NewValue, float) {
            ++CallbackCount;
            LastNewValue = NewValue;
        });
    TestTrue(TEXT("Owner 订阅返回有效句柄"), Handle.IsValid());
    TestEqual(TEXT("Owner 订阅即回放当前值"), CallbackCount, 1);
    TestEqual(TEXT("回放值为当前值"), LastNewValue, 80.0f);

    Comp->ModifyStat(ArmorTag, -10.0f);
    TestEqual(TEXT("Owner 存活时收到更新"), CallbackCount, 2);
    TestEqual(TEXT("更新值正确"), LastNewValue, 70.0f);

    Owner->MarkAsGarbage();
    Comp->ModifyStat(ArmorTag, -10.0f);
    TestEqual(TEXT("Owner 销毁后不再回调"), CallbackCount, 2);

    Comp->UnsubscribeStatChanged(ArmorTag, Handle);
    Comp->ModifyStat(ArmorTag, -10.0f);
    TestEqual(TEXT("显式退订后不再回调"), CallbackCount, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatModifierTest,
    "Game.GameplayCore.Stat.ModifierStack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameStatModifierTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Modifier.Attack")));

    Comp->RegisterStat(AttackTag, 100.0f, 100.0f, 0.0f);
    TestEqual(TEXT("无修正器时派生值等于基础值"), Comp->GetStatEffectiveCurrent(AttackTag), 100.0f);

    const uint32 AddHandle = Comp->AddStatModifier(AttackTag, FGameStatModifier(20.0f, EGameStatModifierOp::Add));
    const uint32 MultiplyHandle = Comp->AddStatModifier(AttackTag, FGameStatModifier(2.0f, EGameStatModifierOp::Multiply));
    TestEqual(TEXT("加算后乘算的派生值正确"), Comp->GetStatEffectiveCurrent(AttackTag), 240.0f);
    TestEqual(TEXT("基础值不被修正器改写"), Comp->GetStatCurrent(AttackTag), 100.0f);

    TestTrue(TEXT("按句柄移除加算修正器"), Comp->RemoveStatModifier(AttackTag, AddHandle));
    TestEqual(TEXT("移除加算后仅剩乘算"), Comp->GetStatEffectiveCurrent(AttackTag), 200.0f);

    const uint32 OverrideHandle = Comp->AddStatModifier(AttackTag, FGameStatModifier(7.0f, EGameStatModifierOp::Override));
    TestEqual(TEXT("覆写修正器最终生效"), Comp->GetStatEffectiveCurrent(AttackTag), 7.0f);

    TestEqual(TEXT("清空全部修正器"), Comp->ClearStatModifiers(AttackTag), 2);
    TestEqual(TEXT("清空后派生值回落到基础值"), Comp->GetStatEffectiveCurrent(AttackTag), 100.0f);
    TestFalse(TEXT("已移除句柄不可重复移除"), Comp->RemoveStatModifier(AttackTag, MultiplyHandle));
    TestFalse(TEXT("已移除覆写句柄不可重复移除"), Comp->RemoveStatModifier(AttackTag, OverrideHandle));

    int32 ModifierBroadcastCount = 0;
    float LastBroadcastNew = 0.0f;
    float LastBroadcastMax = 0.0f;
    Comp->SubscribeStatChanged(
        AttackTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda(
            [&ModifierBroadcastCount, &LastBroadcastNew, &LastBroadcastMax](const FGameplayTag&, float, float NewValue, float MaxValue) {
                ++ModifierBroadcastCount;
                LastBroadcastNew = NewValue;
                LastBroadcastMax = MaxValue;
            }
        )
    );
    TestEqual(TEXT("初始订阅回放一次"), ModifierBroadcastCount, 1);

    const uint32 NotifyHandle = Comp->AddStatModifier(AttackTag, FGameStatModifier(25.0f, EGameStatModifierOp::Add));
    TestEqual(TEXT("新增修正器触发订阅广播"), ModifierBroadcastCount, 2);
    TestEqual(TEXT("广播新有效值正确"), LastBroadcastNew, 125.0f);
    TestTrue(TEXT("移除修正器成功"), Comp->RemoveStatModifier(AttackTag, NotifyHandle));
    TestEqual(TEXT("移除修正器触发订阅广播"), ModifierBroadcastCount, 3);
    TestEqual(TEXT("移除后广播有效值回落"), LastBroadcastNew, 100.0f);

    const FGameplayTag HealthMaxOnlyTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Modifier.HealthMaxOnly")));
    Comp->RegisterStat(HealthMaxOnlyTag, 0.0f, 100.0f, 0.0f);
    Comp->AddStatModifier(HealthMaxOnlyTag, FGameStatModifier(50.0f, EGameStatModifierOp::Add, false, true));
    TestEqual(TEXT("仅作用于上限的修正器不抬高零血当前值"), Comp->GetStatEffectiveCurrent(HealthMaxOnlyTag), 0.0f);
    TestEqual(TEXT("仅作用于上限的修正器正确提升有效上限"), Comp->GetStatEffectiveMax(HealthMaxOnlyTag), 150.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatProxyReviveAndVitalTest,
    "Game.GameplayCore.Stat.ProxyReviveAndVital",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameStatProxyReviveAndVitalTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Proxy.VitalHealth")));

    float ExternalCurrent = 100.0f;
    float ExternalMax = 100.0f;

    Comp->BindNativeStatQueries(
        FGameStatQueryDelegate::CreateLambda([&ExternalCurrent](const FGameplayTag&) { return ExternalCurrent; }),
        FGameStatQueryDelegate::CreateLambda([&ExternalMax](const FGameplayTag&) { return ExternalMax; })
    );

    Comp->BindNativeStatWriter(FGameStatWriteDelegate::CreateLambda(
        [&ExternalCurrent, &ExternalMax](const FGameplayTag&, EGameStatWriteOp Op, float Value, float& OutApplied) {
            if (Op == EGameStatWriteOp::Modify) {
                const float Prev = ExternalCurrent;
                ExternalCurrent = FMath::Clamp(ExternalCurrent + Value, 0.0f, ExternalMax);
                OutApplied = ExternalCurrent - Prev;
                return true;
            }
            if (Op == EGameStatWriteOp::SetCurrent) {
                ExternalCurrent = FMath::Clamp(Value, 0.0f, ExternalMax);
                OutApplied = 0.0f;
                return true;
            }
            return false;
        }
    ));

    float ActualDelta = 0.0f;
    Comp->ModifyStat(HealthTag, -40.0f, ActualDelta);
    TestEqual(TEXT("外部数值已扣减为 60"), ExternalCurrent, 60.0f);
    TestEqual(TEXT("实际增量应为 -40"), ActualDelta, -40.0f);

    Comp->SetStatCurrent(HealthTag, 80.0f);
    TestEqual(TEXT("代理设置当前值成功同步为 80"), ExternalCurrent, 80.0f);

    const FGameStatModifier MultMod = FGameStatModifier::MakeMultiplier(1.5f);
    TestEqual(TEXT("MakeMultiplier 构造的 Op 为 Multiply"), static_cast<uint8>(MultMod.Op), static_cast<uint8>(EGameStatModifierOp::Multiply));
    TestEqual(TEXT("MakeMultiplier 构造的值正确"), MultMod.Value, 1.5f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatFullApiCoverageTest,
    "Game.GameplayCore.Stat.FullApiCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGameStatFullApiCoverageTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag TagA = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Test.CovA")));
    const FGameplayTag TagB = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Test.CovB")));

    Comp->RegisterStat(TagA, 50.0f, 100.0f, 0.0f);
    Comp->RegisterStat(TagB, 20.0f, 50.0f, 0.0f);

    TArray<FGameplayTag> AllTags;
    Comp->GetAllStatTags(AllTags);
    TestEqual(TEXT("已注册属性标签总数为 2"), AllTags.Num(), 2);
    TestTrue(TEXT("包含标签 A"), AllTags.Contains(TagA));
    TestTrue(TEXT("包含标签 B"), AllTags.Contains(TagB));

    FGameStatValue ValueA;
    TestTrue(TEXT("读取存在的属性成功"), Comp->GetStatValue(TagA, ValueA));
    TestEqual(TEXT("Current 值为 50"), ValueA.Current, 50.0f);
    TestEqual(TEXT("Max 值为 100"), ValueA.Max, 100.0f);

    FGameStatValue MissingValue;
    const FGameplayTag InvalidTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Test.Invalid")));
    TestFalse(TEXT("读取未注册属性返回 false"), Comp->GetStatValue(InvalidTag, MissingValue));

    TestNearlyEqual(TEXT("有效百分比应为 0.5"), Comp->GetStatEffectivePercent(TagA), 0.5f, 0.0001f);
    TestNearlyEqual(TEXT("未注册属性有效百分比应为 0.0"), Comp->GetStatEffectivePercent(InvalidTag), 0.0f, 0.0001f);

    TestTrue(TEXT("注销属性 TagA 成功"), Comp->UnregisterStat(TagA));
    TestFalse(TEXT("TagA 不再存在"), Comp->HasStat(TagA));
    TestFalse(TEXT("重复注销 TagA 返回 false"), Comp->UnregisterStat(TagA));

    Comp->GetAllStatTags(AllTags);
    TestEqual(TEXT("注销后标签总数变为 1"), AllTags.Num(), 1);
    TestFalse(TEXT("不再包含标签 A"), AllTags.Contains(TagA));
    TestTrue(TEXT("仍包含标签 B"), AllTags.Contains(TagB));

    Comp->BindStatSource(nullptr);
    TestFalse(TEXT("未绑定代理写接口时 CanWriteStat 返回 false"), Comp->CanWriteStat());

    return true;
}

#endif

