/**
 * @file UGameVitalComponentTest.cpp
 * @brief UGameVitalComponent 自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Stat/UGameStatComponent.h"
#include "Stat/UGameVitalComponent.h"

#if WITH_AUTOMATION_TESTS

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

#endif
