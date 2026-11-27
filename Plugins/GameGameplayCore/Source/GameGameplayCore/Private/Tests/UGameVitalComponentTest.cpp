/**
 * @file UGameVitalComponentTest.cpp
 * @brief UGameVitalComponent 生命状态机、CDO 预配置订阅、显式解绑、蓝图动态委托与网络去重自动化测试
 */

#include "CoreMinimal.h"
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

namespace {

class UTestGameVitalComponent : public UGameVitalComponent {
public:
    using UGameVitalComponent::OnRep_VitalAlive;
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameVitalLifecycleTest,
    "Game.GameplayCore.Vital.LifecycleAndRevive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证主生命扣血致死、百分比复活以及蓝图动态与原生双轨广播
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameVitalLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* StatComp = NewObject<UGameStatComponent>();
    UGameVitalComponent* VitalComp = NewObject<UGameVitalComponent>();
    UGameGameplayCoreTestReceiver* Receiver = NewObject<UGameGameplayCoreTestReceiver>();

    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Vital.TestHealth")));
    StatComp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);

    VitalComp->SetPrimaryVitalTag(HealthTag);
    VitalComp->BindStatComponent(StatComp);

    TestTrue(TEXT("初始状态应该存活"), VitalComp->IsVitalAlive());

    int32 VitalChangeCount = 0;
    bool bLastReportedAlive = true;
    VitalComp->OnVitalStateChangedNative.AddLambda([&VitalChangeCount, &bLastReportedAlive](bool bAlive, const FGameplayTag&) {
        ++VitalChangeCount;
        bLastReportedAlive = bAlive;
    });
    VitalComp->OnVitalStateChanged.AddDynamic(Receiver, &UGameGameplayCoreTestReceiver::HandleVitalStateChanged);

    float ActualDelta = 0.0f;
    StatComp->ModifyStat(HealthTag, -100.0f, ActualDelta);

    TestEqual(TEXT("生命值应归零"), StatComp->GetStatEffectiveCurrent(HealthTag), 0.0f);
    TestFalse(TEXT("生命归零后应判定死亡"), VitalComp->IsVitalAlive());
    TestEqual(TEXT("应广播一次死亡状态"), VitalChangeCount, 1);
    TestFalse(TEXT("广播内容应为死亡"), bLastReportedAlive);
    TestEqual(TEXT("动态多播委托收到一次死亡广播"), Receiver->VitalChangedCount, 1);
    TestFalse(TEXT("动态多播委托记录状态为死亡"), Receiver->bLastVitalAlive);
    TestEqual(TEXT("动态多播委托标签一致"), Receiver->LastVitalTag, HealthTag);

    VitalComp->ReviveVital(0.5f);

    TestEqual(TEXT("复活半血应为 50"), StatComp->GetStatEffectiveCurrent(HealthTag), 50.0f);
    TestTrue(TEXT("复活后应恢复存活"), VitalComp->IsVitalAlive());
    TestEqual(TEXT("应广播复活事件"), VitalChangeCount, 2);
    TestTrue(TEXT("广播内容应为存活"), bLastReportedAlive);
    TestEqual(TEXT("动态多播委托收到复活广播"), Receiver->VitalChangedCount, 2);
    TestTrue(TEXT("动态多播委托记录状态为存活"), Receiver->bLastVitalAlive);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameVitalRepAliveDeduplicationTest,
    "Game.GameplayCore.Vital.RepAliveDeduplication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证属性归零触发死亡后再收到相同 OnRep_VitalAlive 时不重复广播
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameVitalRepAliveDeduplicationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* StatComp = NewObject<UGameStatComponent>();
    UTestGameVitalComponent* VitalComp = NewObject<UTestGameVitalComponent>();

    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Vital.RepAliveTest")));
    StatComp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);

    VitalComp->SetPrimaryVitalTag(HealthTag);
    VitalComp->BindStatComponent(StatComp);

    int32 VitalChangeCount = 0;
    VitalComp->OnVitalStateChangedNative.AddLambda([&VitalChangeCount](bool, const FGameplayTag&) {
        ++VitalChangeCount;
    });

    float ActualDelta = 0.0f;
    StatComp->ModifyStat(HealthTag, -100.0f, ActualDelta);
    TestEqual(TEXT("扣血致死广播一次"), VitalChangeCount, 1);

    VitalComp->OnRep_VitalAlive();
    TestEqual(TEXT("状态未变动时 OnRep_VitalAlive 不重复广播"), VitalChangeCount, 1);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameVitalRepAliveForwardBroadcastTest,
    "Game.GameplayCore.Vital.RepAliveForwardBroadcast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证客户端仅收到存活布尔复制时能正常触发死亡与复活广播且具备幂等去重
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameVitalRepAliveForwardBroadcastTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameVitalComponent* VitalComp = NewObject<UGameVitalComponent>();
    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Vital.RepAliveForward")));
    VitalComp->SetPrimaryVitalTag(HealthTag);

    int32 ChangeCount = 0;
    bool bObservedAlive = true;
    VitalComp->OnVitalStateChangedNative.AddLambda([&ChangeCount, &bObservedAlive](bool bNewAlive, const FGameplayTag&) {
        ++ChangeCount;
        bObservedAlive = bNewAlive;
    });

    VitalComp->bVitalAlive = false;
    VitalComp->OnRep_VitalAlive();

    TestEqual(TEXT("客户端仅收到存活布尔复制时正常触发死亡广播"), ChangeCount, 1);
    TestFalse(TEXT("广播状态为已阵亡"), bObservedAlive);
    TestFalse(TEXT("内部记录广播状态同步"), VitalComp->bLastBroadcastedAlive);

    VitalComp->OnRep_VitalAlive();
    TestEqual(TEXT("未变动时 OnRep 不重复广播"), ChangeCount, 1);

    VitalComp->bVitalAlive = true;
    VitalComp->OnRep_VitalAlive();
    TestEqual(TEXT("客户端收到存活布尔复活时正常触发复活广播"), ChangeCount, 2);
    TestTrue(TEXT("广播状态为存活"), bObservedAlive);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameVitalActorLifecycleTest,
    "Game.GameplayCore.Vital.ActorLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证真实 Actor 挂接、BeginPlay 自动发现数值组件、EndPlay 解绑与显式 BindStatComponent(nullptr) 隔离
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameVitalActorLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestHostActor Host;
    TestTrue(TEXT("宿主 Actor 创建成功"), Host.IsValidHost());
    if (!Host.IsValidHost()) {
        return false;
    }

    UGameStatComponent* StatComp = NewObject<UGameStatComponent>(Host.Get());
    UGameVitalComponent* VitalComp = NewObject<UGameVitalComponent>(Host.Get());
    Host.Get()->AddInstanceComponent(StatComp);
    Host.Get()->AddInstanceComponent(VitalComp);
    StatComp->RegisterComponent();
    VitalComp->RegisterComponent();

    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Vital.AutoResolve")));
    StatComp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);
    VitalComp->SetPrimaryVitalTag(HealthTag);
    VitalComp->BeginPlay();

    int32 StateChangeCount = 0;
    VitalComp->OnVitalStateChangedNative.AddLambda([&StateChangeCount](bool, const FGameplayTag&) {
        ++StateChangeCount;
    });

    float ActualDelta = 0.0f;
    StatComp->ModifyStat(HealthTag, -100.0f, ActualDelta);
    TestEqual(TEXT("BeginPlay 自动绑定宿主数值组件后，扣血致死广播成功"), StateChangeCount, 1);
    TestFalse(TEXT("实体已阵亡"), VitalComp->IsVitalAlive());

    VitalComp->BindStatComponent(nullptr);
    StatComp->SetStatCurrent(HealthTag, 100.0f);
    VitalComp->EvaluateVitalState();
    TestFalse(TEXT("显式 BindStatComponent(nullptr) 后不再偷偷重绑宿主数值组件"), VitalComp->IsVitalAlive());
    TestEqual(TEXT("解绑后不再触发状态变更"), StateChangeCount, 1);

    VitalComp->BindStatComponent(StatComp);
    TestTrue(TEXT("重新显式绑定后立即同步恢复存活"), VitalComp->IsVitalAlive());

    VitalComp->EndPlay(EEndPlayReason::Destroyed);
    StatComp->SetStatCurrent(HealthTag, 0.0f);
    TestEqual(TEXT("EndPlay 解绑委托后，数值变动不再触发生命状态机"), StateChangeCount, 2);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameVitalCdoTagSubscriptionTest,
    "Game.GameplayCore.Vital.CdoTagSubscription",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证构造期先绑定 StatComp、随后由 CDO 载入 PrimaryVitalTag 时 BeginPlay 能自动补齐订阅
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameVitalCdoTagSubscriptionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestHostActor Host;
    TestTrue(TEXT("宿主 Actor 创建成功"), Host.IsValidHost());
    if (!Host.IsValidHost()) {
        return false;
    }

    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Vital.CdoTag")));
    UGameStatComponent* StatComp = NewObject<UGameStatComponent>(Host.Get());
    UGameVitalComponent* VitalComp = NewObject<UGameVitalComponent>(Host.Get());
    StatComp->RegisterComponent();
    VitalComp->RegisterComponent();

    StatComp->RegisterStat(HealthTag, 100.0f, 100.0f, 0.0f);
    VitalComp->BindStatComponent(StatComp);
    VitalComp->PrimaryVitalTag = HealthTag;

    VitalComp->BeginPlay();
    StatComp->ModifyStat(HealthTag, -100.0f);
    TestFalse(TEXT("BeginPlay 补齐 CDO 主生命标签订阅后，扣血归零准确判定阵亡"), VitalComp->IsVitalAlive());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameVitalTagSwitchAndBoundaryTest,
    "Game.GameplayCore.Vital.TagSwitchAndBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证主生命标签动态切换、清空恢复默认存活、非有限复活比例拦截及非权威端复活拦截
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameVitalTagSwitchAndBoundaryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* StatComp = NewObject<UGameStatComponent>();
    UGameVitalComponent* VitalComp = NewObject<UGameVitalComponent>();

    const FGameplayTag HealthA = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Vital.SwitchA")));
    const FGameplayTag HealthB = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Vital.SwitchB")));

    StatComp->RegisterStat(HealthA, 100.0f, 100.0f, 0.0f);
    StatComp->RegisterStat(HealthB, 50.0f, 50.0f, 0.0f);

    VitalComp->BindStatComponent(StatComp);
    VitalComp->SetPrimaryVitalTag(HealthA);
    TestEqual(TEXT("读取主生命标签正确"), VitalComp->GetPrimaryVitalTag(), HealthA);

    VitalComp->SetPrimaryVitalTag(HealthA);
    TestEqual(TEXT("相同标签重复设置无副作用"), VitalComp->GetPrimaryVitalTag(), HealthA);

    int32 ChangeCount = 0;
    VitalComp->OnVitalStateChangedNative.AddLambda([&ChangeCount](bool, const FGameplayTag&) {
        ++ChangeCount;
    });

    VitalComp->SetPrimaryVitalTag(HealthB);
    TestEqual(TEXT("切换主生命标签成功"), VitalComp->GetPrimaryVitalTag(), HealthB);

    float Delta = 0.0f;
    StatComp->ModifyStat(HealthA, -100.0f, Delta);
    TestEqual(TEXT("旧标签 HealthA 变动不再影响生命状态机"), ChangeCount, 0);

    StatComp->ModifyStat(HealthB, -50.0f, Delta);
    TestEqual(TEXT("新标签 HealthB 变动触发致死广播"), ChangeCount, 1);
    TestFalse(TEXT("当前判定为已阵亡"), VitalComp->IsVitalAlive());

    const FGameplayTag EmptyTag;
    VitalComp->SetPrimaryVitalTag(EmptyTag);
    TestTrue(TEXT("清空主生命标签后自动恢复存活态"), VitalComp->IsVitalAlive());
    TestEqual(TEXT("恢复存活态触发广播"), ChangeCount, 2);

    VitalComp->SetPrimaryVitalTag(EmptyTag);
    TestEqual(TEXT("重复设置空标签短路早退不触发多余状态变动"), ChangeCount, 2);

    VitalComp->SetPrimaryVitalTag(HealthB);
    TestFalse(TEXT("重新设回已归零标签恢复阵亡态"), VitalComp->IsVitalAlive());

    VitalComp->ReviveVital(std::numeric_limits<float>::quiet_NaN());
    TestEqual(TEXT("NaN 复活比例被安全拦截，生命保持 0"), StatComp->GetStatCurrent(HealthB), 0.0f);
    TestFalse(TEXT("NaN 复活被拦截后仍保持阵亡态"), VitalComp->IsVitalAlive());

    VitalComp->ReviveVital(-0.5f);
    TestEqual(TEXT("负比例复活钳制到 0"), StatComp->GetStatCurrent(HealthB), 0.0f);

    VitalComp->ReviveVital(2.5f);
    TestEqual(TEXT("超额比例复活钳制到 100% (50.0f)"), StatComp->GetStatCurrent(HealthB), 50.0f);

    UGameVitalComponent* UnboundVitalComp = NewObject<UGameVitalComponent>();
    UnboundVitalComp->ReviveVital(1.0f);

    const FGameplayTag UnregisteredTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Vital.Unregistered")));
    VitalComp->SetPrimaryVitalTag(UnregisteredTag);
    VitalComp->ReviveVital(1.0f);
    TestFalse(TEXT("未注册主生命属性复活安全拦截"), StatComp->HasStat(UnregisteredTag));

    const FGameplayTag ZeroRangeTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Vital.ZeroRange")));
    StatComp->RegisterStat(ZeroRangeTag, 10.0f, 10.0f, 10.0f);
    VitalComp->SetPrimaryVitalTag(ZeroRangeTag);
    VitalComp->ReviveVital(1.0f);
    TestEqual(TEXT("Max <= Min 异常范围复活安全拦截"), StatComp->GetStatCurrent(ZeroRangeTag), 10.0f);

    FScopedTestHostActor NonAuthHost;
    TestTrue(TEXT("非权威宿主有效"), NonAuthHost.IsValidHost());
    if (NonAuthHost.IsValidHost()) {
        UGameStatComponent* NonAuthStat = NewObject<UGameStatComponent>(NonAuthHost.Get());
        UGameVitalComponent* NonAuthVital = NewObject<UGameVitalComponent>(NonAuthHost.Get());
        NonAuthStat->RegisterComponent();
        NonAuthVital->RegisterComponent();
        NonAuthStat->RegisterStat(HealthB, 0.0f, 50.0f, 0.0f);
        NonAuthVital->BindStatComponent(NonAuthStat);
        NonAuthVital->SetPrimaryVitalTag(HealthB);
        TestFalse(TEXT("切换模拟代理前已阵亡"), NonAuthVital->IsVitalAlive());

        NonAuthHost.Get()->SetRole(ROLE_SimulatedProxy);
        NonAuthVital->ReviveVital(1.0f);
        TestEqual(TEXT("非权威端拒绝复活，生命保持 0"), NonAuthStat->GetStatCurrent(HealthB), 0.0f);
        TestFalse(TEXT("非权威端拒绝复活，仍保持阵亡"), NonAuthVital->IsVitalAlive());
    }

    return true;
}

#endif
