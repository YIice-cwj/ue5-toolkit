/**
 * @file FGameStatAcknowledgedDiffTest.cpp
 * @brief FGameStatAcknowledgedDiff 网络差分去重自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Stat/FGameStatAcknowledgedDiff.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatAcknowledgedDiffTest,
    "Game.GameplayCore.Stat.AcknowledgedDiff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameStatAcknowledgedDiff 网络复制差分状态计算与客户端未变动过滤去重
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatAcknowledgedDiffTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Diff.Health")));
    FGameStatAcknowledgedDiff Diff;

    float OldCurrent = 0.0f;
    float OldMax = 0.0f;
    TestTrue(TEXT("首次到达需要广播"), Diff.NeedsBroadcast(HealthTag, 80.0f, 100.0f, OldCurrent, OldMax));
    TestEqual(TEXT("首次到达的旧值等于本次当前值"), OldCurrent, 80.0f);
    TestEqual(TEXT("首次到达的旧上限等于本次上限"), OldMax, 100.0f);

    Diff.Record(HealthTag, 80.0f, 100.0f);
    TestFalse(TEXT("数值未变化时无需广播"), Diff.NeedsBroadcast(HealthTag, 80.0f, 100.0f, OldCurrent, OldMax));

    TestTrue(TEXT("当前值变化需要广播"), Diff.NeedsBroadcast(HealthTag, 60.0f, 100.0f, OldCurrent, OldMax));
    TestEqual(TEXT("变化的旧值等于上次已确认值"), OldCurrent, 80.0f);

    Diff.Record(HealthTag, 60.0f, 100.0f);
    TestTrue(TEXT("仅上限变化需要广播"), Diff.NeedsBroadcast(HealthTag, 60.0f, 120.0f, OldCurrent, OldMax));
    TestEqual(TEXT("仅上限变化时旧上限为已确认值"), OldMax, 100.0f);

    Diff.Record(HealthTag, 60.0f, 120.0f);
    Diff.Remove(HealthTag);
    TestTrue(TEXT("移除记录后重新视为首次到达"), Diff.NeedsBroadcast(HealthTag, 60.0f, 120.0f, OldCurrent, OldMax));
    TestEqual(TEXT("移除后旧值重新等于本次值"), OldCurrent, 60.0f);

    Diff.Record(HealthTag, 60.0f, 120.0f);
    TMap<FGameplayTag, int32> RemainingIndex;
    RemainingIndex.Add(HealthTag, 0);
    Diff.PruneMissing(RemainingIndex);
    TestFalse(TEXT("索引仍包含时不清理"), Diff.NeedsBroadcast(HealthTag, 60.0f, 120.0f, OldCurrent, OldMax));

    TMap<FGameplayTag, int32> EmptyIndex;
    Diff.PruneMissing(EmptyIndex);
    TestTrue(TEXT("索引不再包含时清理并视为首次"), Diff.NeedsBroadcast(HealthTag, 60.0f, 120.0f, OldCurrent, OldMax));

    Diff.Reset();
    TestTrue(TEXT("重置后视为首次"), Diff.NeedsBroadcast(HealthTag, 10.0f, 10.0f, OldCurrent, OldMax));

    return true;
}

#endif
