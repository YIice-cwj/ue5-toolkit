/**
 * @file FActorHitStopTable.cpp
 * @brief Actor 局部顿帧表实现
 */

#include "Time/FActorHitStopTable.h"

#include "GameFramework/Actor.h"

void FActorHitStopTable::Request(AActor& InTarget, float InDuration, float InTimeDilation) {
    const float ClampedDilation = FMath::Clamp(InTimeDilation, 0.0001f, 20.0f);
    FActorHitStopState* ExistingState = ActiveActorHitStops.Find(&InTarget);
    if (ExistingState != nullptr) {
        ExistingState->RemainingDuration = FMath::Max(ExistingState->RemainingDuration, InDuration);
        ExistingState->HitStopTimeDilation = FMath::Min(ExistingState->HitStopTimeDilation, ClampedDilation);
        InTarget.CustomTimeDilation = ExistingState->HitStopTimeDilation;
        return;
    }

    FActorHitStopState NewState;
    NewState.TargetActor = &InTarget;
    NewState.OriginalCustomTimeDilation = InTarget.CustomTimeDilation;
    NewState.HitStopTimeDilation = ClampedDilation;
    NewState.RemainingDuration = InDuration;

    InTarget.CustomTimeDilation = ClampedDilation;
    ActiveActorHitStops.Add(&InTarget, NewState);
}

void FActorHitStopTable::Clear(AActor* InTarget) {
    if (InTarget == nullptr) {
        return;
    }

    FActorHitStopState* ExistingState = ActiveActorHitStops.Find(InTarget);
    if (ExistingState == nullptr) {
        return;
    }

    InTarget->CustomTimeDilation = ExistingState->OriginalCustomTimeDilation;
    ActiveActorHitStops.Remove(InTarget);
}

void FActorHitStopTable::ClearAll() {
    for (const TPair<TWeakObjectPtr<AActor>, FActorHitStopState>& Pair : ActiveActorHitStops) {
        if (AActor* Actor = Pair.Key.Get()) {
            Actor->CustomTimeDilation = Pair.Value.OriginalCustomTimeDilation;
        }
    }
    ActiveActorHitStops.Empty();
}

bool FActorHitStopTable::IsActive(const AActor* InTarget) const {
    return InTarget != nullptr && ActiveActorHitStops.Contains(InTarget);
}

void FActorHitStopTable::Advance(float InRealDeltaSeconds) {
    for (auto Iterator = ActiveActorHitStops.CreateIterator(); Iterator; ++Iterator) {
        FActorHitStopState& State = Iterator.Value();
        AActor* Actor = State.TargetActor.Get();
        if (Actor == nullptr) {
            Iterator.RemoveCurrent();
            continue;
        }

        State.RemainingDuration -= InRealDeltaSeconds;
        if (State.RemainingDuration <= 0.0f) {
            Actor->CustomTimeDilation = State.OriginalCustomTimeDilation;
            Iterator.RemoveCurrent();
        }
    }
}

void FActorHitStopTable::PruneInvalid() {
    for (auto Iterator = ActiveActorHitStops.CreateIterator(); Iterator; ++Iterator) {
        if (!Iterator.Key().IsValid()) {
            Iterator.RemoveCurrent();
        }
    }
}
