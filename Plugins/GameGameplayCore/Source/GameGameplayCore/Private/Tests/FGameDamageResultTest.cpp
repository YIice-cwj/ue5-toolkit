/**
 * @file FGameDamageResultTest.cpp
 * @brief FGameDamageResult 打击反馈自动化测试
 */

#include "CoreMinimal.h"
#include "Damage/FGameDamageChannel.h"
#include "Damage/FGameDamageResult.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDamageResultTest,
    "Game.GameplayCore.Damage.ResultFeedback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameDamageResult 反馈标签管理、动态数值参数注入、实际通道总和及正负值通道下的主通道决策
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameDamageResultTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameDamageResult Result;
    Result.SetActualDamage(75.0f);
    TestEqual(TEXT("实际总伤害设置与读取一致"), Result.TotalActualDamage, 75.0f);
    TestEqual(TEXT("实际总伤害 Getter 读取一致"), Result.GetActualDamage(), 75.0f);

    const FGameplayTag HitTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.Hit")));
    const FGameplayTag FatalTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.Fatal")));
    const FGameplayTag CriticalTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.Critical")));
    const FGameplayTag StaggerTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.Stagger")));
    const FGameplayTag HitStopTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.HitStop")));
    const FGameplayTag MissingDynamicTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.MissingDynamic")));

    Result.AddFeedbackTag(HitTag);
    TestTrue(TEXT("Hit tag should exist"), Result.HasFeedbackTag(HitTag));
    TestFalse(TEXT("Fatal tag should not exist"), Result.HasFeedbackTag(FatalTag));

    Result.AddFeedbackTag(CriticalTag);
    TestTrue(TEXT("Feedback tag should exist"), Result.HasFeedbackTag(CriticalTag));
    TestFalse(TEXT("Unadded tag should not exist"), Result.HasFeedbackTag(StaggerTag));

    Result.RemoveFeedbackTag(CriticalTag);
    TestFalse(TEXT("Feedback tag should be removed"), Result.HasFeedbackTag(CriticalTag));

    Result.SetDynamicValue(HitStopTag, 0.12f);
    TestTrue(TEXT("存在动态数值参数"), Result.HasDynamicValue(HitStopTag));
    TestFalse(TEXT("不存在未添加的动态数值"), Result.HasDynamicValue(MissingDynamicTag));
    TestEqual(TEXT("Dynamic value should match"), Result.GetDynamicValue(HitStopTag), 0.12f);
    TestEqual(TEXT("Missing dynamic value should return default"), Result.GetDynamicValue(MissingDynamicTag, 5.0f), 5.0f);
    TestTrue(TEXT("精确移除动态数值参数成功"), Result.RemoveDynamicValue(HitStopTag));
    TestFalse(TEXT("移除后不再存在该动态数值"), Result.HasDynamicValue(HitStopTag));
    TestFalse(TEXT("重复移除不存在动态数值返回 false"), Result.RemoveDynamicValue(HitStopTag));

    const FGameplayTag SlashTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Result.Slash")));
    const FGameplayTag BluntTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Result.Blunt")));
    const FGameplayTag PhysTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Result")));

    TestFalse(TEXT("空通道容器返回无效主通道"), Result.GetDominantActualChannel().IsValid());
    TestEqual(TEXT("空通道实际伤害总和为 0"), Result.GetTotalActualChannelDamage(), 0.0f);

    Result.SetActualChannelDamage(BluntTag, 50.0f);
    Result.SetActualChannelDamage(SlashTag, 100.0f);
    TestEqual(TEXT("主通道选择最大实际伤害通道"), Result.GetDominantActualChannel(), SlashTag);
    TestEqual(TEXT("精确读取实际通道伤害正确"), Result.GetActualChannelDamageExact(SlashTag), 100.0f);
    TestEqual(TEXT("层级求和读取实际通道伤害正确"), Result.GetActualChannelDamage(PhysTag), 150.0f);
    TestEqual(TEXT("所有实际通道总和计算正确"), Result.GetTotalActualChannelDamage(), 150.0f);

    Result.AddActualChannelDamage(SlashTag, 20.0f);
    TestEqual(TEXT("累加实际通道伤害正确"), Result.GetActualChannelDamageExact(SlashTag), 120.0f);
    Result.SetActualChannelDamage(SlashTag, 100.0f);

    TestTrue(TEXT("精确移除实际伤害通道成功"), Result.RemoveActualChannel(SlashTag));
    TestEqual(TEXT("移除后实际通道数减少"), Result.ActualChannels.Num(), 1);
    TestEqual(TEXT("移除后主通道迁移至剩余通道"), Result.GetDominantActualChannel(), BluntTag);
    TestEqual(TEXT("移除后总实际通道伤害同步减少"), Result.GetTotalActualChannelDamage(), 50.0f);
    TestFalse(TEXT("重复移除未存在通道返回 false"), Result.RemoveActualChannel(SlashTag));

    FGameDamageResult NegativeResult;
    NegativeResult.SetActualChannelDamage(SlashTag, -20.0f);
    NegativeResult.SetActualChannelDamage(BluntTag, -5.0f);
    TestEqual(TEXT("负值通道仍按数值最大者选择"), NegativeResult.GetDominantActualChannel(), BluntTag);

    return true;
}

#endif
