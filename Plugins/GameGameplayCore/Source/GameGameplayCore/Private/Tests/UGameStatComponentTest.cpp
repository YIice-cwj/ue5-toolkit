/**
 * @file UGameStatComponentTest.cpp
 * @brief UGameStatComponent 属性组件核心生命周期、数值钳制、蓝图门面、错误码与网络复制自动化测试
 */

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Stat/UGameStatComponent.h"
#include "Tests/GameGameplayCoreTestUtils.h"
#include "Tests/UGameGameplayCoreTestReceiver.h"

#include <limits>

#if WITH_AUTOMATION_TESTS

using Game::GameplayCore::Test::FScopedTestHostActor;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatMultiRegistrationTest,
    "Game.GameplayCore.Stat.RegistrationAndClamping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多属性动态注册、数值加减在 Min/Max 范围内的自动钳制与实际变动差值记录
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
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

/**
 * @brief 验证极端增量越界输入下的 Clamp 防御与非有限浮点 NaN/Inf 写边界全拦截
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatBoundaryClampingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Health.ClampTest")));
    const FGameplayTag BadTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Health.NonFinite")));

    const float QuietNaN = std::numeric_limits<float>::quiet_NaN();
    const float PosInf = std::numeric_limits<float>::infinity();

    Comp->RegisterStat(BadTag, QuietNaN, 100.0f, 0.0f);
    TestFalse(TEXT("RegisterStat 拒绝 NaN 初始值"), Comp->HasStat(BadTag));
    Comp->RegisterStat(BadTag, 50.0f, PosInf, 0.0f);
    TestFalse(TEXT("RegisterStat 拒绝 Inf 上限"), Comp->HasStat(BadTag));

    Comp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);
    TestEqual(TEXT("初始属性值正确"), Comp->GetStatCurrent(HealthTag), 100.0f);

    float ActualDelta = 0.0f;
    Comp->ModifyStat(HealthTag, -150.0f, ActualDelta);
    TestEqual(TEXT("数值扣减超出下限时应 Clamp 到 Min"), Comp->GetStatCurrent(HealthTag), 0.0f);
    TestEqual(TEXT("实际增量应为 -100"), ActualDelta, -100.0f);

    TestFalse(TEXT("ModifyStat 拒绝 NaN 增量防止静默满血复活"), Comp->ModifyStat(HealthTag, QuietNaN, ActualDelta));
    TestEqual(TEXT("NaN 增量后仍保持 0 血"), Comp->GetStatCurrent(HealthTag), 0.0f);
    TestFalse(TEXT("ModifyStat 拒绝 +Inf 增量"), Comp->ModifyStat(HealthTag, PosInf, ActualDelta));

    Comp->SetStatCurrent(HealthTag, QuietNaN);
    TestEqual(TEXT("SetStatCurrent 拒绝 NaN"), Comp->GetStatCurrent(HealthTag), 0.0f);

    Comp->SetStatMax(HealthTag, QuietNaN, true);
    TestEqual(TEXT("SetStatMax 拒绝 NaN"), Comp->GetStatMax(HealthTag), 100.0f);

    const TGameStatusResult<FGameStatModifierHandle> BadMod = Comp->AddStatModifier(HealthTag, FGameStatModifier(QuietNaN));
    TestTrue(TEXT("AddStatModifier 拒绝 NaN 修正值"), BadMod.IsFailure());
    TestEqual(TEXT("非有限修正值错误码为 InvalidParameter"), BadMod.GetError().ErrorCode, Game::GameplayCoreErrorCodes::InvalidParameter);

    Comp->ModifyStat(HealthTag, 200.0f, ActualDelta);
    TestEqual(TEXT("数值增加超出上限时应 Clamp 到 Max"), Comp->GetStatCurrent(HealthTag), 100.0f);
    TestEqual(TEXT("实际增量应为 100"), ActualDelta, 100.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatSetMaxClampingTest,
    "Game.GameplayCore.Stat.SetMaxClamping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证动态调整属性上限（等比缩放 / 绝对截断）时当前值钳制不变式及 Override 下抑制无效广播
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatSetMaxClampingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Stat.SetMaxClamp")));
    Comp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);

    Comp->SetStatMax(HealthTag, 0.0f, true);
    TestEqual(TEXT("上限下调至下限时上限等于下限"), Comp->GetStatMax(HealthTag), 0.0f);
    TestEqual(TEXT("上限下调至下限时当前值钳制到下限"), Comp->GetStatCurrent(HealthTag), 0.0f);

    Comp->SetStatMax(HealthTag, 100.0f, false);
    Comp->SetStatCurrent(HealthTag, 80.0f);
    Comp->SetStatMax(HealthTag, 50.0f, false);
    TestEqual(TEXT("非等比缩放时当前值钳制至新上限"), Comp->GetStatCurrent(HealthTag), 50.0f);
    TestEqual(TEXT("新上限为 50"), Comp->GetStatMax(HealthTag), 50.0f);

    Comp->AddStatModifier(HealthTag, FGameStatModifier(77.0f, EGameStatModifierOp::Override, true, true));
    int32 BroadcastCount = 0;
    Comp->OnStatChangedNative.AddLambda([&BroadcastCount](const FGameplayTag&, float, float, float) {
        ++BroadcastCount;
    });
    Comp->SetStatMax(HealthTag, 120.0f, false);
    TestEqual(TEXT("双轴被 Override 锁定且有效值未变时不产生多余广播"), BroadcastCount, 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatReplicationTest,
    "Game.GameplayCore.Stat.Replication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 OnRep_ReplicatedStats 客户端网络复制数组解析、稳态索引短路、差分广播与清理
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatReplicationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag TagA = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Rep.TagA")));
    const FGameplayTag TagB = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Rep.TagB")));

    int32 BroadcastCountA = 0;
    int32 BroadcastCountB = 0;
    float LastValueA = 0.0f;

    Comp->SubscribeStatChanged(TagA, FOnGameStatChangedNative::FDelegate::CreateLambda(
        [&BroadcastCountA, &LastValueA](const FGameplayTag&, float, float NewVal, float) {
            ++BroadcastCountA;
            LastValueA = NewVal;
        }
    ));
    Comp->SubscribeStatChanged(TagB, FOnGameStatChangedNative::FDelegate::CreateLambda(
        [&BroadcastCountB](const FGameplayTag&, float, float, float) {
            ++BroadcastCountB;
        }
    ));

    Comp->ReplicatedStats.Emplace(TagA, FGameStatValue{80.0f, 100.0f, 0.0f});
    Comp->ReplicatedStats.Emplace(TagB, FGameStatValue{40.0f, 50.0f, 0.0f});

    Comp->OnRep_ReplicatedStats();

    TestTrue(TEXT("复制到达后索引重建成功，包含 TagA"), Comp->HasStat(TagA));
    TestTrue(TEXT("复制到达后索引重建成功，包含 TagB"), Comp->HasStat(TagB));
    TestEqual(TEXT("首次复制到达 TagA 广播 1 次"), BroadcastCountA, 1);
    TestEqual(TEXT("首次复制到达 TagB 广播 1 次"), BroadcastCountB, 1);
    TestEqual(TEXT("TagA 初始广播值正确"), LastValueA, 80.0f);

    Comp->OnRep_ReplicatedStats();
    TestEqual(TEXT("数值未变化时 OnRep 不重复广播 TagA"), BroadcastCountA, 1);
    TestEqual(TEXT("数值未变化时 OnRep 不重复广播 TagB"), BroadcastCountB, 1);

    Comp->ReplicatedStats[0].Value.Current = 60.0f;
    Comp->OnRep_ReplicatedStats();
    TestEqual(TEXT("数值变化后 OnRep 广播 TagA"), BroadcastCountA, 2);
    TestEqual(TEXT("TagA 变化后广播新值"), LastValueA, 60.0f);
    TestEqual(TEXT("TagB 未变动不广播"), BroadcastCountB, 1);

    Comp->ReplicatedStats.RemoveAt(1);
    Comp->OnRep_ReplicatedStats();
    TestFalse(TEXT("移除条目后 OnRep 成功剔除 TagB"), Comp->HasStat(TagB));

    Comp->ReplicatedStats.Emplace(TagB, FGameStatValue{25.0f, 50.0f, 0.0f});
    Comp->ReplicatedStats[0].Value.Current = 45.0f;
    bool bTriggeredReallocOnRep = false;
    Comp->SubscribeStatChanged(TagA, FOnGameStatChangedNative::FDelegate::CreateLambda(
        [Comp, &bTriggeredReallocOnRep](const FGameplayTag&, float, float NewVal, float) {
            if (!bTriggeredReallocOnRep && FMath::IsNearlyEqual(NewVal, 45.0f)) {
                bTriggeredReallocOnRep = true;
                for (int32 Index = 0; Index < 16; ++Index) {
                    const FGameplayTag ExtraTag = UGameplayTagsManager::Get().AddNativeGameplayTag(
                        FName(*FString::Printf(TEXT("Attribute.Rep.Realloc.%d"), Index)));
                    Comp->RegisterStat(ExtraTag, 1.0f, 10.0f, 0.0f);
                }
            }
        }
    ));
    Comp->OnRep_ReplicatedStats();
    TestTrue(TEXT("OnRep 回调内重入扩容 ReplicatedStats 正常触发"), bTriggeredReallocOnRep);
    TestEqual(TEXT("OnRep 两阶段派发在数组重分配后仍安全送达 TagB 广播"), BroadcastCountB, 2);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatDefensiveBranchesTest,
    "Game.GameplayCore.Stat.DefensiveBranches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证无效标签拦截、零增量忽略、未注册属性保护、精确错误码契约及非权威端写入拦截
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatDefensiveBranchesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    const FGameplayTag InvalidTag;
    const FGameplayTag ValidTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Defensive.Valid")));
    const FGameplayTag MissingTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Defensive.Missing")));

    Comp->RegisterStat(InvalidTag, 100.0f, 100.0f, 0.0f);
    TestFalse(TEXT("无效标签注册被拒绝"), Comp->HasStat(InvalidTag));

    const TGameStatusResult<void> InvalidUnreg = Comp->UnregisterStat(InvalidTag);
    TestTrue(TEXT("无效标签注销返回失败"), InvalidUnreg.IsFailure());
    TestEqual(TEXT("无效标签注销模块 ID 为 Gameplay"), InvalidUnreg.GetError().ModuleId, Game::ModuleId::Gameplay);
    TestEqual(TEXT("无效标签注销错误码为 InvalidParameter"), InvalidUnreg.GetError().ErrorCode, Game::GameplayCoreErrorCodes::InvalidParameter);

    Comp->RegisterStat(ValidTag, 100.0f, 100.0f, 0.0f);
    TestTrue(TEXT("有效标签注册成功"), Comp->HasStat(ValidTag));

    Comp->RegisterStat(ValidTag, 80.0f, 120.0f, 0.0f);
    TestEqual(TEXT("重复注册同一标签更新数值"), Comp->GetStatCurrent(ValidTag), 80.0f);

    float Delta = 0.0f;
    TestFalse(TEXT("未注册标签 ModifyStat 返回 false"), Comp->ModifyStat(MissingTag, 10.0f, Delta));
    TestFalse(TEXT("修改零增量返回 false"), Comp->ModifyStat(ValidTag, 0.0f, Delta));

    Comp->SetStatCurrent(ValidTag, 100.0f);
    TestEqual(TEXT("设置相同值后当前值保持不变"), Comp->GetStatCurrent(ValidTag), 100.0f);

    Comp->SetStatMax(ValidTag, 100.0f, false);
    TestEqual(TEXT("设置相同上限后上限保持不变"), Comp->GetStatMax(ValidTag), 100.0f);

    TestEqual(TEXT("未注册属性有效上限为 0"), Comp->GetStatEffectiveMax(MissingTag), 0.0f);
    TestEqual(TEXT("未注册属性基础上限为 0"), Comp->GetStatMax(MissingTag), 0.0f);
    TestEqual(TEXT("未注册属性基础下限为 0"), Comp->GetStatMin(MissingTag), 0.0f);
    TestEqual(TEXT("未注册属性当前值为 0"), Comp->GetStatCurrent(MissingTag), 0.0f);
    TestEqual(TEXT("未注册属性有效当前值为 0"), Comp->GetStatEffectiveCurrent(MissingTag), 0.0f);

    const TGameStatusResult<FGameStatModifierHandle> MissingHandle =
        Comp->AddStatModifier(MissingTag, FGameStatModifier(10.0f, EGameStatModifierOp::Add));
    TestTrue(TEXT("未注册属性添加修正器分配有效代际句柄"), MissingHandle.IsSuccess() && MissingHandle.GetValue().IsValid());
    TestTrue(TEXT("移除未注册属性修正器返回成功"), Comp->RemoveStatModifier(MissingTag, MissingHandle.GetValue()).IsSuccess());
    TestEqual(TEXT("未注册属性清空修正器返回 0"), Comp->ClearStatModifiers(MissingTag), 0);

    FScopedTestHostActor NonAuthHost;
    TestTrue(TEXT("非权威测试宿主创建成功"), NonAuthHost.IsValidHost());
    if (NonAuthHost.IsValidHost()) {
        NonAuthHost.Get()->SetRole(ROLE_SimulatedProxy);
        UGameStatComponent* NonAuthComp = NewObject<UGameStatComponent>(NonAuthHost.Get());
        NonAuthComp->RegisterComponent();

        TestFalse(TEXT("非权威端默认不允许写入"), NonAuthComp->IsAuthoritativeWriteAllowed());
        NonAuthComp->RegisterStat(ValidTag, 100.0f, 100.0f, 0.0f);
        TestFalse(TEXT("非权威端默认禁止注册属性"), NonAuthComp->HasStat(ValidTag));

        NonAuthComp->bAllowNonAuthoritativeWrites = true;
        TestTrue(TEXT("开启非权威写入后允许写入"), NonAuthComp->IsAuthoritativeWriteAllowed());
        NonAuthComp->RegisterStat(ValidTag, 50.0f, 100.0f, 0.0f);
        TestTrue(TEXT("开启非权威写入后允许注册"), NonAuthComp->HasStat(ValidTag));

        NonAuthComp->bAllowNonAuthoritativeWrites = false;
        TestFalse(TEXT("关闭非权威写入后拒绝修改"), NonAuthComp->ModifyStat(ValidTag, 10.0f, Delta));
        const TGameStatusResult<void> NonAuthUnreg = NonAuthComp->UnregisterStat(ValidTag);
        TestTrue(TEXT("关闭非权威写入后拒绝注销"), NonAuthUnreg.IsFailure());
        TestEqual(TEXT("非权威注销错误码为 NotAuthority"), NonAuthUnreg.GetError().ErrorCode, Game::GameplayCoreErrorCodes::NotAuthority);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatFullApiCoverageTest,
    "Game.GameplayCore.Stat.FullApiCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 UGameStatComponent 全部对外公开 API、蓝图 K2 门面、动态多播委托与 O(1) Swap 注销索引一致性
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatFullApiCoverageTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Comp = NewObject<UGameStatComponent>();
    UGameGameplayCoreTestReceiver* Receiver = NewObject<UGameGameplayCoreTestReceiver>();
    Comp->OnStatChanged.AddDynamic(Receiver, &UGameGameplayCoreTestReceiver::HandleStatChanged);

    const FGameplayTag TagA = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Test.CovA")));
    const FGameplayTag TagB = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Test.CovB")));
    const FGameplayTag TagC = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Test.CovC")));

    Comp->RegisterStat(TagA, 50.0f, 100.0f, 0.0f);
    Comp->RegisterStat(TagB, 20.0f, 50.0f, 0.0f);
    Comp->RegisterStat(TagC, 30.0f, 60.0f, 0.0f);
    TestEqual(TEXT("动态多播委托收到三次注册广播"), Receiver->StatChangedCount, 3);
    TestEqual(TEXT("动态多播委托最新标签为 TagC"), Receiver->LastStatTag, TagC);

    TArray<FGameplayTag> AllTags;
    Comp->GetAllStatTags(AllTags);
    TestEqual(TEXT("已注册属性标签总数为 3"), AllTags.Num(), 3);
    TestTrue(TEXT("包含标签 A"), AllTags.Contains(TagA));
    TestTrue(TEXT("包含标签 B"), AllTags.Contains(TagB));
    TestTrue(TEXT("包含标签 C"), AllTags.Contains(TagC));

    FGameStatValue K2Value;
    TestTrue(TEXT("K2_GetStatValue 读取存在属性成功"), Comp->K2_GetStatValue(TagA, K2Value));
    TestEqual(TEXT("K2_GetStatValue Current 为 50"), K2Value.Current, 50.0f);

    const FGameplayTag InvalidTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Test.Invalid")));
    TestFalse(TEXT("K2_GetStatValue 读取未注册属性返回 false"), Comp->K2_GetStatValue(InvalidTag, K2Value));
    const TGameStatusResult<FGameStatValue> MissingRes = Comp->GetStatValue(InvalidTag);
    TestTrue(TEXT("读取未注册属性返回失败"), MissingRes.IsFailure());
    TestEqual(TEXT("未注册属性错误码为 StatNotFound"), MissingRes.GetError().ErrorCode, Game::GameplayCoreErrorCodes::StatNotFound);

    TestNearlyEqual(TEXT("有效百分比应为 0.5"), Comp->GetStatEffectivePercent(TagA), 0.5f, 0.0001f);
    TestNearlyEqual(TEXT("未注册属性有效百分比应为 0.0"), Comp->GetStatEffectivePercent(InvalidTag), 0.0f, 0.0001f);

    const int64 PackedHandle = Comp->K2_AddStatModifier(TagA, FGameStatModifier(20.0f, EGameStatModifierOp::Add));
    TestTrue(TEXT("K2_AddStatModifier 返回非零打包句柄"), PackedHandle != 0);
    TestEqual(TEXT("蓝图修正器生效后派生值为 70"), Comp->GetStatEffectiveCurrent(TagA), 70.0f);
    TestEqual(TEXT("动态多播委托收到修正器变化通知"), Receiver->LastStatNew, 70.0f);
    TestTrue(TEXT("K2_UpdateStatModifier 原地更新成功"), Comp->K2_UpdateStatModifier(TagA, PackedHandle, FGameStatModifier(35.0f, EGameStatModifierOp::Add)));
    TestEqual(TEXT("蓝图原地更新修正器后派生值为 85"), Comp->GetStatEffectiveCurrent(TagA), 85.0f);
    TestFalse(TEXT("K2_UpdateStatModifier 零句柄安全返回 false"), Comp->K2_UpdateStatModifier(TagA, 0, FGameStatModifier(10.0f, EGameStatModifierOp::Add)));
    TestTrue(TEXT("K2_RemoveStatModifier 按打包句柄注销成功"), Comp->K2_RemoveStatModifier(TagA, PackedHandle));
    TestFalse(TEXT("K2_RemoveStatModifier 零句柄安全返回 false"), Comp->K2_RemoveStatModifier(TagA, 0));
    TestEqual(TEXT("注销蓝图修正器后派生值回落 50"), Comp->GetStatEffectiveCurrent(TagA), 50.0f);

    const FGameplayTag PreModTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Test.PreModRegister")));
    Comp->AddStatModifier(PreModTag, FGameStatModifier(50.0f, EGameStatModifierOp::Add));
    float FirstRegOld = -1.0f;
    float FirstRegNew = -1.0f;
    Comp->SubscribeStatChanged(
        PreModTag,
        FOnGameStatChangedNative::FDelegate::CreateLambda([&FirstRegOld, &FirstRegNew](const FGameplayTag&, float OldVal, float NewVal, float) {
            FirstRegOld = OldVal;
            FirstRegNew = NewVal;
        })
    );
    Comp->RegisterStat(PreModTag, 100.0f, 100.0f, 0.0f);
    TestEqual(TEXT("预挂修正器首次注册广播旧值等于首次有效值 150"), FirstRegOld, 150.0f);
    TestEqual(TEXT("预挂修正器首次注册广播新值等于首次有效值 150"), FirstRegNew, 150.0f);
    Comp->UnregisterStat(PreModTag);

    Comp->AddStatModifier(TagA, FGameStatModifier(20.0f, EGameStatModifierOp::Add));
    TestTrue(TEXT("K2_UnregisterStat 注销中间属性 TagA 成功"), Comp->K2_UnregisterStat(TagA));
    TestFalse(TEXT("TagA 不再存在"), Comp->HasStat(TagA));
    TestFalse(TEXT("重复 K2_UnregisterStat 返回 false"), Comp->K2_UnregisterStat(TagA));
    TestEqual(TEXT("RemoveAtSwap 后尾部 TagC 索引保持正确"), Comp->GetStatCurrent(TagC), 30.0f);
    TestEqual(TEXT("RemoveAtSwap 后 TagB 索引保持正确"), Comp->GetStatCurrent(TagB), 20.0f);

    Comp->RegisterStat(TagA, 50.0f, 100.0f, 0.0f);
    TestEqual(TEXT("重新注册后历史修正器已被注销清理"), Comp->GetStatEffectiveCurrent(TagA), 50.0f);
    TestTrue(TEXT("再次注销 TagA"), Comp->UnregisterStat(TagA).IsSuccess());

    Comp->GetAllStatTags(AllTags);
    TestEqual(TEXT("注销后标签总数变为 2"), AllTags.Num(), 2);
    TestFalse(TEXT("不再包含标签 A"), AllTags.Contains(TagA));
    TestTrue(TEXT("仍包含标签 B"), AllTags.Contains(TagB));
    TestTrue(TEXT("仍包含标签 C"), AllTags.Contains(TagC));

    const FGameStatModifier MultMod = FGameStatModifier::MakeMultiplier(1.5f);
    TestEqual(TEXT("MakeMultiplier 构造的 Op 为 Multiply"), static_cast<uint8>(MultMod.Op), static_cast<uint8>(EGameStatModifierOp::Multiply));
    TestEqual(TEXT("MakeMultiplier 构造的值正确"), MultMod.Value, 1.5f);

    return true;
}

#endif
