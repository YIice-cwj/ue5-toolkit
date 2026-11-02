// Copyright Epic Games, Inc. All Rights Reserved.

#include "Adapter/GameFeedbackAdapter.h"

/**
 * @brief 从伤害分发上下文与结算结果提取标准视听表现信号
 * @param InTarget 受击目标实体（显式传入）
 * @param Context 伤害上下文
 * @param Result 伤害结算结果
 * @return 转换后的视听表现信号
 */
FGameFeedbackSignal UGameFeedbackAdapter::MakeSignalFromDamage(
    AActor* InTarget,
    const FGameDamageContext& Context,
    const FGameDamageResult& Result)
{
    FGameFeedbackSignal Signal;
    Signal.Instigator = Context.Instigator;
    Signal.DamageCauser = Context.DamageCauser;
    Signal.Target = InTarget ? InTarget : Context.HitResult.GetActor();
    Signal.HitResult = Context.HitResult;

    if (AActor* Causer = Context.DamageCauser.Get()) {
        const FVector Vel = Causer->GetVelocity();
        Signal.ImpactDirection = Vel.IsNearlyZero() ? FVector(Context.HitResult.ImpactNormal) : Vel.GetSafeNormal();
    } else {
        Signal.ImpactDirection = FVector(Context.HitResult.ImpactNormal);
    }

    Signal.TraceDirection = (Context.HitResult.TraceEnd - Context.HitResult.TraceStart).GetSafeNormal();
    Signal.FeedbackTags = Result.FeedbackTags;

    Signal.PrimaryChannel = Result.GetDominantActualChannel();
    Signal.Magnitude = Result.TotalActualDamage;
    Signal.bLethal = Result.FeedbackTags.HasTag(TAG_Feedback_State_Lethal);

    return Signal;
}
