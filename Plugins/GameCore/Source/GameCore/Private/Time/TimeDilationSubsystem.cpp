// Copyright Epic Games, Inc. All Rights Reserved.

#include "Time/TimeDilationSubsystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"

UTimeDilationSubsystem::UTimeDilationSubsystem() {
    ChannelStates.SetNum(static_cast<int32>(ETimeDilationChannel::Count));
}

UTimeDilationSubsystem& UTimeDilationSubsystem::Get(const UObject* WorldContextObject) {
    UTimeDilationSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

UTimeDilationSubsystem* UTimeDilationSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
        return World->GetSubsystem<UTimeDilationSubsystem>();
    }
    return nullptr;
}

void UTimeDilationSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    LastRealTime = FPlatformTime::Seconds();
    EffectiveGlobalTimeDilation = 1.0f;
    ChannelStates.SetNum(static_cast<int32>(ETimeDilationChannel::Count));
    ActiveActorHitStops.Empty();
}

void UTimeDilationSubsystem::Deinitialize() {
    ClearAllHitStops();
    ClearAllGlobalDilations();
    Super::Deinitialize();
}

void UTimeDilationSubsystem::Tick(float DeltaTime) {
    Super::Tick(DeltaTime);

    const double CurrentRealTime = FPlatformTime::Seconds();
    float RealDeltaSeconds = (LastRealTime > 0.0) ? static_cast<float>(CurrentRealTime - LastRealTime) : DeltaTime;
    LastRealTime = CurrentRealTime;

    if (const UWorld* World = GetWorld(); World && World->IsPaused()) {
        return;
    }

    RealDeltaSeconds = FMath::Clamp(RealDeltaSeconds, 0.0f, 1.0f);
    AdvanceTime(RealDeltaSeconds);
}

TStatId UTimeDilationSubsystem::GetStatId() const {
    RETURN_QUICK_DECLARE_CYCLE_STAT(UTimeDilationSubsystem, STATGROUP_Tickables);
}

bool UTimeDilationSubsystem::IsTickableWhenPaused() const {
    return true;
}

void UTimeDilationSubsystem::RequestHitStop(AActor* Target, float Duration, float TimeDilation) {
    if (!Target || Duration <= 0.0f) {
        return;
    }

    const float ClampedDilation = FMath::Clamp(TimeDilation, 0.0001f, 20.0f);
    FActorHitStopState* ExistingState = ActiveActorHitStops.Find(Target);
    if (ExistingState) {
        ExistingState->RemainingDuration = FMath::Max(ExistingState->RemainingDuration, Duration);
        ExistingState->HitStopTimeDilation = FMath::Min(ExistingState->HitStopTimeDilation, ClampedDilation);
        Target->CustomTimeDilation = ExistingState->HitStopTimeDilation;
    } else {
        FActorHitStopState NewState;
        NewState.TargetActor = Target;
        NewState.OriginalCustomTimeDilation = Target->CustomTimeDilation;
        NewState.HitStopTimeDilation = ClampedDilation;
        NewState.RemainingDuration = Duration;

        Target->CustomTimeDilation = ClampedDilation;
        ActiveActorHitStops.Add(Target, NewState);
    }
}

void UTimeDilationSubsystem::ClearHitStop(AActor* Target) {
    if (!Target) {
        return;
    }

    FActorHitStopState* ExistingState = ActiveActorHitStops.Find(Target);
    if (ExistingState) {
        Target->CustomTimeDilation = ExistingState->OriginalCustomTimeDilation;
        ActiveActorHitStops.Remove(Target);
    }
}

bool UTimeDilationSubsystem::IsActorInHitStop(const AActor* Target) const {
    return Target && ActiveActorHitStops.Contains(Target);
}

void UTimeDilationSubsystem::ClearAllHitStops() {
    for (auto& Pair : ActiveActorHitStops) {
        AActor* Actor = Pair.Key.Get();
        if (Actor) {
            Actor->CustomTimeDilation = Pair.Value.OriginalCustomTimeDilation;
        }
    }
    ActiveActorHitStops.Empty();
}

void UTimeDilationSubsystem::RequestGlobalDilation(ETimeDilationChannel Channel, float Duration, float TimeDilation, float BlendOutDuration) {
    const int32 Index = static_cast<int32>(Channel);
    if (!ChannelStates.IsValidIndex(Index)) {
        return;
    }

    FGlobalChannelState& State = ChannelStates[Index];
    State.TargetDilation = FMath::Clamp(TimeDilation, 0.0f, 20.0f);
    State.CurrentDilation = State.TargetDilation;
    State.TotalDuration = FMath::Max(0.0f, Duration);
    State.RemainingDuration = State.TotalDuration;
    State.BlendOutDuration = FMath::Max(0.0f, BlendOutDuration);
    State.RemainingBlendOut = 0.0f;
    State.bActive = true;

    UpdateEffectiveGlobalTimeDilation();
}

void UTimeDilationSubsystem::ClearGlobalDilation(ETimeDilationChannel Channel) {
    const int32 Index = static_cast<int32>(Channel);
    if (ChannelStates.IsValidIndex(Index) && ChannelStates[Index].bActive) {
        ChannelStates[Index].Reset();
        UpdateEffectiveGlobalTimeDilation();
    }
}

void UTimeDilationSubsystem::ClearGlobalDilationWithBlend(ETimeDilationChannel Channel, float BlendOutDuration) {
    const int32 Index = static_cast<int32>(Channel);
    if (ChannelStates.IsValidIndex(Index) && ChannelStates[Index].bActive) {
        if (BlendOutDuration <= 0.0f) {
            ClearGlobalDilation(Channel);
            return;
        }

        FGlobalChannelState& State = ChannelStates[Index];
        State.TargetDilation = State.CurrentDilation;
        State.RemainingDuration = 0.0f;
        State.BlendOutDuration = BlendOutDuration;
        State.RemainingBlendOut = BlendOutDuration;
        UpdateEffectiveGlobalTimeDilation();
    }
}

void UTimeDilationSubsystem::ClearAllGlobalDilations() {
    bool bNeedsUpdate = false;
    for (FGlobalChannelState& State : ChannelStates) {
        if (State.bActive) {
            State.Reset();
            bNeedsUpdate = true;
        }
    }
    if (bNeedsUpdate) {
        UpdateEffectiveGlobalTimeDilation();
    }
}

float UTimeDilationSubsystem::GetEffectiveGlobalTimeDilation() const {
    return EffectiveGlobalTimeDilation;
}

bool UTimeDilationSubsystem::IsChannelActive(ETimeDilationChannel Channel) const {
    const int32 Index = static_cast<int32>(Channel);
    return ChannelStates.IsValidIndex(Index) ? ChannelStates[Index].bActive : false;
}

float UTimeDilationSubsystem::GetChannelTimeDilation(ETimeDilationChannel Channel) const {
    const int32 Index = static_cast<int32>(Channel);
    return (ChannelStates.IsValidIndex(Index) && ChannelStates[Index].bActive) ? ChannelStates[Index].CurrentDilation : 1.0f;
}

void UTimeDilationSubsystem::AdvanceTime(float RealDeltaSeconds) {
    if (RealDeltaSeconds <= 0.0f) {
        return;
    }

    bool bGlobalChannelsChanged = false;

    for (int32 Index = 0; Index < ChannelStates.Num(); ++Index) {
        FGlobalChannelState& State = ChannelStates[Index];
        if (!State.bActive) {
            continue;
        }

        if (State.RemainingDuration > 0.0f) {
            State.RemainingDuration -= RealDeltaSeconds;
            if (State.RemainingDuration <= 0.0f) {
                State.RemainingDuration = 0.0f;
                if (State.BlendOutDuration > 0.0f) {
                    State.RemainingBlendOut = State.BlendOutDuration;
                } else {
                    State.Reset();
                    bGlobalChannelsChanged = true;
                }
            }
        } else if (State.RemainingBlendOut > 0.0f) {
            State.RemainingBlendOut -= RealDeltaSeconds;
            if (State.RemainingBlendOut <= 0.0f) {
                State.Reset();
                bGlobalChannelsChanged = true;
            } else {
                const float Alpha = FMath::Clamp(1.0f - (State.RemainingBlendOut / State.BlendOutDuration), 0.0f, 1.0f);
                State.CurrentDilation = FMath::Lerp(State.TargetDilation, 1.0f, Alpha);
                bGlobalChannelsChanged = true;
            }
        }
    }

    if (bGlobalChannelsChanged) {
        UpdateEffectiveGlobalTimeDilation();
    }

    for (auto It = ActiveActorHitStops.CreateIterator(); It; ++It) {
        FActorHitStopState& State = It.Value();
        AActor* Actor = State.TargetActor.Get();
        if (!Actor) {
            It.RemoveCurrent();
            continue;
        }

        State.RemainingDuration -= RealDeltaSeconds;
        if (State.RemainingDuration <= 0.0f) {
            Actor->CustomTimeDilation = State.OriginalCustomTimeDilation;
            It.RemoveCurrent();
        }
    }
}

void UTimeDilationSubsystem::UpdateEffectiveGlobalTimeDilation() {
    float NewEffectiveDilation = 1.0f;

    for (int32 Index = 0; Index < ChannelStates.Num(); ++Index) {
        const FGlobalChannelState& State = ChannelStates[Index];
        if (State.bActive) {
            NewEffectiveDilation = State.CurrentDilation;
            break;
        }
    }

    if (!FMath::IsNearlyEqual(EffectiveGlobalTimeDilation, NewEffectiveDilation)) {
        EffectiveGlobalTimeDilation = NewEffectiveDilation;
        ApplyGlobalTimeDilationToWorld(EffectiveGlobalTimeDilation);
    }
}

void UTimeDilationSubsystem::ApplyGlobalTimeDilationToWorld(float NewTimeDilation) {
    if (UWorld* World = GetWorld()) {
        if (AWorldSettings* WorldSettings = World->GetWorldSettings()) {
            WorldSettings->SetTimeDilation(NewTimeDilation);
        }
    }
}
