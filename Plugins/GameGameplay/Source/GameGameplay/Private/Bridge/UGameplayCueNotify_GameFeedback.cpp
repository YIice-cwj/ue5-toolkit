/**
 * @file UGameplayCueNotify_GameFeedback.cpp
 * @brief 将 GAS GameplayCue 统一收敛至 GameFeedback 中枢的通用桥接器实现
 */

#include "Bridge/UGameplayCueNotify_GameFeedback.h"

#include "Adapter/GameFeedbackAdapter.h"
#include "Bus/GameFeedbackBlueprintLibrary.h"
#include "Damage/FGameGameplayEffectContext.h"
#include "Feedback/GameFeedbackSignal.h"

/**
 * @brief 触发 GameplayCue 执行回调（优化分支构建路径，避免无效字段覆盖）
 * @param MyTarget 目标实体
 * @param Parameters Cue 携带的参数上下文
 * @return 执行是否成功
 */
bool UGameplayCueNotify_GameFeedback::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const {
    if (MyTarget == nullptr) {
        return false;
    }

    FHitResult EffectiveHitResult;
    EffectiveHitResult.ImpactPoint = Parameters.Location;
    EffectiveHitResult.ImpactNormal = Parameters.Normal;
    if (const FHitResult* Hit = Parameters.EffectContext.GetHitResult()) {
        EffectiveHitResult = *Hit;
    }

    FGameFeedbackSignal Signal;
    const FGameplayEffectContext* RawContext = Parameters.EffectContext.Get();
    if (RawContext != nullptr && RawContext->GetScriptStruct()->IsChildOf(FGameGameplayEffectContext::StaticStruct())) {
        const FGameGameplayEffectContext* GameContext = static_cast<const FGameGameplayEffectContext*>(RawContext);
        FGameDamageContext DamageContext;
        DamageContext.Instigator = Parameters.Instigator.Get();
        DamageContext.DamageCauser = Parameters.EffectCauser.Get();
        DamageContext.HitResult = EffectiveHitResult;
        Signal = UGameFeedbackAdapter::MakeSignalFromDamage(MyTarget, DamageContext, GameContext->ToDamageResult());
        Signal.PredictionEventId = GameContext->GetFeedbackEventId();
    } else {
        Signal.Target = MyTarget;
        Signal.Instigator = Parameters.Instigator.Get();
        Signal.DamageCauser = Parameters.EffectCauser.Get();
        Signal.HitResult = EffectiveHitResult;
        Signal.Magnitude = Parameters.RawMagnitude;
        Signal.ImpactDirection = Parameters.Normal;
        Signal.PrimaryChannel = Parameters.MatchedTagName;
    }

    Signal.TraceDirection = (Signal.HitResult.TraceEnd - Signal.HitResult.TraceStart).GetSafeNormal();
    UGameFeedbackBlueprintLibrary::BroadcastFeedback(MyTarget, Signal);
    return true;
}
