/**
 * @file UGameVitalComponentTest.cpp
 * @brief UGameVitalComponent 自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Stat/UGameStatComponent.h"
#include "Stat/UGameVitalComponent.h"

#if WITH_AUTOMATION_TESTS

namespace {

UWorld* GetVitalTestWorld() {
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World() != nullptr) {
            return Context.World();
        }
    }
    return nullptr;
}

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

bool FGameVitalLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* StatComp = NewObject<UGameStatComponent>();
    UGameVitalComponent* VitalComp = NewObject<UGameVitalComponent>();

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

    float ActualDelta = 0.0f;
    StatComp->ModifyStat(HealthTag, -100.0f, ActualDelta);

    TestEqual(TEXT("生命值应归零"), StatComp->GetStatEffectiveCurrent(HealthTag), 0.0f);
    TestFalse(TEXT("生命归零后应判定死亡"), VitalComp->IsVitalAlive());
    TestEqual(TEXT("应广播一次死亡状态"), VitalChangeCount, 1);
    TestFalse(TEXT("广播内容应为死亡"), bLastReportedAlive);

    VitalComp->ReviveVital(0.5f);

    TestEqual(TEXT("复活半血应为 50"), StatComp->GetStatEffectiveCurrent(HealthTag), 50.0f);
    TestTrue(TEXT("复活后应恢复存活"), VitalComp->IsVitalAlive());
    TestEqual(TEXT("应广播复活事件"), VitalChangeCount, 2);
    TestTrue(TEXT("广播内容应为存活"), bLastReportedAlive);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameVitalRepAliveDeduplicationTest,
    "Game.GameplayCore.Vital.RepAliveDeduplication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

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

bool FGameVitalActorLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetVitalTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* HostActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
    TestNotNull(TEXT("宿主 Actor 创建成功"), HostActor);
    if (HostActor == nullptr) {
        return false;
    }

    UGameStatComponent* StatComp = NewObject<UGameStatComponent>(HostActor);
    UGameVitalComponent* VitalComp = NewObject<UGameVitalComponent>(HostActor);
    HostActor->AddInstanceComponent(StatComp);
    HostActor->AddInstanceComponent(VitalComp);
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

    VitalComp->EndPlay(EEndPlayReason::Destroyed);
    StatComp->SetStatCurrent(HealthTag, 100.0f);
    TestEqual(TEXT("EndPlay 解绑委托后，数值变动不再触发生命状态机"), StateChangeCount, 1);

    HostActor->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameVitalTagSwitchAndBoundaryTest,
    "Game.GameplayCore.Vital.TagSwitchAndBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

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

    VitalComp->SetPrimaryVitalTag(HealthB);
    TestFalse(TEXT("重新设回已归零标签恢复阵亡态"), VitalComp->IsVitalAlive());

    VitalComp->ReviveVital(-0.5f);
    TestEqual(TEXT("负比例复活钳制到 0"), StatComp->GetStatCurrent(HealthB), 0.0f);

    VitalComp->ReviveVital(2.5f);
    TestEqual(TEXT("超额比例复活钳制到 100% (50.0f)"), StatComp->GetStatCurrent(HealthB), 50.0f);

    UGameVitalComponent* UnboundVitalComp = NewObject<UGameVitalComponent>();
    UnboundVitalComp->ReviveVital(1.0f);

    const FGameplayTag ZeroRangeTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Vital.ZeroRange")));
    StatComp->RegisterStat(ZeroRangeTag, 10.0f, 10.0f, 10.0f);
    VitalComp->SetPrimaryVitalTag(ZeroRangeTag);
    VitalComp->ReviveVital(1.0f);
    TestEqual(TEXT("Max <= Min 异常范围复活安全拦截"), StatComp->GetStatCurrent(ZeroRangeTag), 10.0f);

    return true;
}

#endif
