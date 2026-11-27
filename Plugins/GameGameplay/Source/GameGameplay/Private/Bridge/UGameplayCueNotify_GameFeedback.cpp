/**
 * @file UGameplayCueNotify_GameFeedback.cpp
 * @brief 将 GAS GameplayCue 统一收敛至 GameFeedback 中枢的通用桥接器实现
 */

#include "Bridge/UGameplayCueNotify_GameFeedback.h"

#include "Bridge/UGameFeedbackAdapter.h"
#include "Bus/GameFeedbackBlueprintLibrary.h"
#include "Damage/FGameGameplayEffectContext.h"
#include "Feedback/GameFeedbackSignal.h"
#include "GameGameplayLog.h"

/**
 * @brief 触发 GameplayCue 执行回调（复用安全上下文提取、消除冗余字段赋值与重复向量归一化）
 * @param MyTarget 目标实体
 * @param Parameters Cue 携带的参数上下文
 * @return 执行是否成功
 */
bool UGameplayCueNotify_GameFeedback::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const {
    if (MyTarget == nullptr) {
        GAME_GAS_LOG(LogGameGasBridge, Verbose, TEXT("【Cue 桥接】目标 Actor 为空，跳过表现信号派发。"));
        return false;
    }

    FHitResult EffectiveHitResult;
    if (const FHitResult* Hit = Parameters.EffectContext.GetHitResult()) {
        EffectiveHitResult = *Hit;
    } else {
        EffectiveHitResult.ImpactPoint = Parameters.Location;
        EffectiveHitResult.ImpactNormal = Parameters.Normal;
    }

    AActor* ResolvedInstigator = Parameters.GetInstigator();
    AActor* ResolvedCauser = Parameters.GetEffectCauser();

    FGameFeedbackSignal Signal;
    if (const FGameGameplayEffectContext* GameContext = FGameGameplayEffectContext::ExtractEffectContext(Parameters.EffectContext)) {
        FGameDamageContext DamageContext;
        DamageContext.Instigator = ResolvedInstigator;
        DamageContext.DamageCauser = ResolvedCauser;
        DamageContext.HitResult = EffectiveHitResult;
        Signal = UGameFeedbackAdapter::MakeSignalFromDamage(MyTarget, DamageContext, GameContext->ToDamageResult());
        Signal.PredictionEventId = GameContext->GetFeedbackEventId();
    } else {
        Signal.Target = MyTarget;
        Signal.Instigator = ResolvedInstigator;
        Signal.DamageCauser = ResolvedCauser;
        Signal.HitResult = EffectiveHitResult;
        Signal.Magnitude = Parameters.RawMagnitude;
        Signal.ImpactDirection = Parameters.Normal.IsNearlyZero() ? FVector(EffectiveHitResult.ImpactNormal) : FVector(Parameters.Normal);
        Signal.PrimaryChannel = Parameters.MatchedTagName;
        Signal.TraceDirection = (Signal.HitResult.TraceEnd - Signal.HitResult.TraceStart).GetSafeNormal();
    }

    UGameFeedbackBlueprintLibrary::BroadcastFeedback(MyTarget, Signal);
    return true;
}
