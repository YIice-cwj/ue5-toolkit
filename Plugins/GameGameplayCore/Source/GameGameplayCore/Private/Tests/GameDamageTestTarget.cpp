// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/GameDamageTestTarget.h"
#include "GameplayTagsManager.h"

/**
 * @brief 响应伤害分发测试实现
 * @param Context 伤害分发上下文
 * @return 打击反馈结果
 */
FGameDamageResult UGameDamageTestTarget::ApplyDamageContext_Implementation(const FGameDamageContext& Context) {
    FGameDamageResult Result;
    Result.SetActualDamage(Context.GetTotalBaseDamage() * 0.8f);

    const FGameplayTag HitTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.TestHit")));
    Result.AddFeedbackTag(HitTag);

    const FGameplayTag SlashTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Test.Physical.Slash")));
    const float SlashDamage = Context.GetChannelDamageExact(SlashTag);
    if (SlashDamage > 0.0f) {
        FGameDamageChannel::SetExact(Result.ActualChannels, SlashTag, SlashDamage * 0.8f);
    }

    const FGameplayTag HitStopTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.TestHitStop")));
    Result.SetDynamicValue(HitStopTag, 0.06f);

    return Result;
}
