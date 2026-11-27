/**
 * @file UGameDamageTestTarget.cpp
 * @brief 私有自动化测试用 IDamageable 受击目标实现
 */

#include "Tests/UGameDamageTestTarget.h"

#include "GameplayTagsManager.h"

/**
 * @brief 响应伤害分发测试实现
 * @param Context 伤害分发上下文
 * @return 打击反馈结果
 */
FGameDamageResult UGameDamageTestTarget::ApplyDamageContext_Implementation(const FGameDamageContext& Context) {
    static const FGameplayTag HitTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.TestHit")));
    static const FGameplayTag SlashTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Test.Physical.Slash")));
    static const FGameplayTag HitStopTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.TestHitStop")));

    FGameDamageResult Result;
    Result.SetActualDamage(Context.GetTotalBaseDamage() * 0.8f);
    Result.AddFeedbackTag(HitTag);

    const float SlashDamage = Context.GetChannelDamageExact(SlashTag);
    if (SlashDamage > 0.0f) {
        Result.SetActualChannelDamage(SlashTag, SlashDamage * 0.8f);
    }

    Result.SetDynamicValue(HitStopTag, 0.06f);
    return Result;
}
