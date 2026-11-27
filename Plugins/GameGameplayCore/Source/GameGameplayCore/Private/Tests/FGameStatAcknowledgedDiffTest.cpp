/**
 * @file FGameStatAcknowledgedDiffTest.cpp
 * @brief FGameStatAcknowledgedDiff 网络差分去重自动化测试
 */

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Stat/FGameStatAcknowledgedDiff.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStatAcknowledgedDiffTest,
    "Game.GameplayCore.Stat.AcknowledgedDiff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameStatAcknowledgedDiff 网络复制差分状态计算、FastTagKey 查找与客户端未变动过滤去重
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameStatAcknowledgedDiffTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameplayTag HealthTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Diff.Health")));
    const FGameplayTag ManaTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Attribute.Diff.Mana")));
    FGameStatAcknowledgedDiff Diff;

    float OldCurrent = 0.0f;
    float OldMax = 0.0f;
    TestTrue(TEXT("首次到达需要广播"), Diff.NeedsBroadcast(HealthTag, 80.0f, 100.0f, OldCurrent, OldMax));
    TestEqual(TEXT("首次到达的旧值等于本次当前值"), OldCurrent, 80.0f);
    TestEqual(TEXT("首次到达的旧上限等于本次上限"), OldMax, 100.0f);

    Diff.Record(HealthTag, 80.0f, 100.0f);
    Diff.Record(ManaTag, 50.0f, 50.0f);
    TestFalse(TEXT("数值未变化时无需广播"), Diff.NeedsBroadcast(HealthTag, 80.0f, 100.0f, OldCurrent, OldMax));
    TestFalse(TEXT("多键隔离且 Mana 未变化不广播"), Diff.NeedsBroadcast(ManaTag, 50.0f, 50.0f, OldCurrent, OldMax));

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
    Diff.PruneMissing([&HealthTag](const FGameplayTag& Tag) {
        return Tag.MatchesTagExact(HealthTag);
    });
    TestFalse(TEXT("谓词保留时不清理 HealthTag"), Diff.NeedsBroadcast(HealthTag, 60.0f, 120.0f, OldCurrent, OldMax));
    TestTrue(TEXT("谓词剔除的 ManaTag 清理后视为首次"), Diff.NeedsBroadcast(ManaTag, 50.0f, 50.0f, OldCurrent, OldMax));

    Diff.PruneMissing([](const FGameplayTag&) {
        return false;
    });
    TestTrue(TEXT("谓词全剔除后 HealthTag 重新视为首次"), Diff.NeedsBroadcast(HealthTag, 60.0f, 120.0f, OldCurrent, OldMax));

    Diff.Record(HealthTag, 10.0f, 10.0f);
    Diff.Reset();
    TestTrue(TEXT("重置后视为首次"), Diff.NeedsBroadcast(HealthTag, 10.0f, 10.0f, OldCurrent, OldMax));

    TSortedVectorMap<FGameFastTagKey, int32, 4> FastKeyMap;
    const FGameplayTag EmptyTag;
    FastKeyMap.Add(FGameFastTagKey(EmptyTag), 1);
    FastKeyMap.Add(FGameFastTagKey(EmptyTag), 2);
    TestEqual(TEXT("FGameFastTagKey 空标签满足严格弱序等价性，重复插入覆写不膨胀"), FastKeyMap.Num(), 1);
    const int32* FoundEmpty = FastKeyMap.Find(FGameFastTagKey(EmptyTag));
    TestNotNull(TEXT("FGameFastTagKey 空标签可被准确查回"), FoundEmpty);
    if (FoundEmpty != nullptr) {
        TestEqual(TEXT("空标签覆写后的最新值正确"), *FoundEmpty, 2);
    }
    TestTrue(TEXT("FGameFastTagKey 空标签可被准确移除"), FastKeyMap.Remove(FGameFastTagKey(EmptyTag)));
    TestEqual(TEXT("移除空标签后容器归零"), FastKeyMap.Num(), 0);

    return true;
}

#endif
