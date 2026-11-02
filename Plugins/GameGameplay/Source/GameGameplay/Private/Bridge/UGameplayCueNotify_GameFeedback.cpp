// Copyright Epic Games, Inc. All Rights Reserved.

#include "Bridge/UGameplayCueNotify_GameFeedback.h"

#include "Adapter/GameFeedbackAdapter.h"
#include "Bus/GameFeedbackBlueprintLibrary.h"
#include "Damage/FGameGameplayEffectContext.h"
#include "Feedback/GameFeedbackSignal.h"

/**
 * @brief 触发 GameplayCue 执行回调
 * @param MyTarget 目标实体
 * @param Parameters Cue 携带的参数上下文
 * @return 执行是否成功
 */
bool UGameplayCueNotify_GameFeedback::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
    if (!MyTarget) {
        return false;
    }

    FGameFeedbackSignal Signal;
    Signal.Target = MyTarget;
    Signal.Instigator = Parameters.Instigator.Get();
    Signal.DamageCauser = Parameters.EffectCauser.Get();
    Signal.HitResult.ImpactPoint = Parameters.Location;
    Signal.HitResult.ImpactNormal = Parameters.Normal;
    if (const FHitResult* Hit = Parameters.EffectContext.GetHitResult()) {
        Signal.HitResult = *Hit;
    }
    Signal.Magnitude = Parameters.RawMagnitude;
    Signal.ImpactDirection = Parameters.Normal;
    Signal.PrimaryChannel = Parameters.MatchedTagName;

    if (const FGameplayEffectContext* RawContext = Parameters.EffectContext.Get()) {
        if (RawContext->GetScriptStruct()->IsChildOf(FGameGameplayEffectContext::StaticStruct())) {
            const FGameGameplayEffectContext* GameContext = static_cast<const FGameGameplayEffectContext*>(RawContext);
            FGameDamageContext DamageContext;
            DamageContext.Instigator = Parameters.Instigator.Get();
            DamageContext.DamageCauser = Parameters.EffectCauser.Get();
            DamageContext.HitResult = Signal.HitResult;
            Signal = UGameFeedbackAdapter::MakeSignalFromDamage(MyTarget, DamageContext, GameContext->ToDamageResult());
            Signal.PredictionEventId = GameContext->GetFeedbackEventId();
        }
    }

    Signal.TraceDirection = (Signal.HitResult.TraceEnd - Signal.HitResult.TraceStart).GetSafeNormal();
    UGameFeedbackBlueprintLibrary::BroadcastFeedback(MyTarget, Signal);
    return true;
}
