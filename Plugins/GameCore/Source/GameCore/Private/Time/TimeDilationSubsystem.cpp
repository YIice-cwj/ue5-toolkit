// Copyright Epic Games, Inc. All Rights Reserved.

#include "Time/TimeDilationSubsystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"
#include "Config/GameCompileSwitches.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameTimeDilation, Log, All);

UTimeDilationSubsystem::UTimeDilationSubsystem() = default;

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
    LastAppliedWorldDilation = 1.0f;
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameTimeDilation, Log, TEXT("【时间膨胀】时间膨胀子系统初始化就绪。"));
    }
}

void UTimeDilationSubsystem::Deinitialize() {
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameTimeDilation, Log, TEXT("【时间膨胀】子系统注销，清理所有定格与通道膨胀。"));
    }
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
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameTimeDilation, Warning, TEXT("【时间膨胀】请求定格失败：目标为空或时长 <= 0。"));
        }
        return;
    }

    HitStopTable.Request(*Target, Duration, TimeDilation);

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameTimeDilation, Log, TEXT("【时间膨胀】请求定格成功: 目标=[%s], 时长=%.3f 秒, 膨胀系数=%.3f"),
            *GetNameSafe(Target), Duration, Target->CustomTimeDilation);
    }
}

void UTimeDilationSubsystem::ClearHitStop(AActor* Target) {
    if (!Target) {
        return;
    }

    HitStopTable.Clear(Target);

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameTimeDilation, Verbose, TEXT("【时间膨胀】恢复定格目标: [%s], 恢复膨胀系数=%.3f"),
            *GetNameSafe(Target), Target->CustomTimeDilation);
    }
}

bool UTimeDilationSubsystem::IsActorInHitStop(const AActor* Target) const {
    return HitStopTable.IsActive(Target);
}

void UTimeDilationSubsystem::ClearAllHitStops() {
    HitStopTable.ClearAll();
}

void UTimeDilationSubsystem::RequestGlobalDilation(ETimeDilationChannel Channel, float Duration, float TimeDilation, float BlendOutDuration) {
    DilationMixer.Request(Channel, Duration, TimeDilation, BlendOutDuration);
    SyncWorldDilation();

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameTimeDilation, Log, TEXT("【时间膨胀】设置全局通道膨胀: 通道=%d, 系数=%.3f, 时长=%.3f 秒, 淡出时长=%.3f 秒"),
            static_cast<int32>(Channel), DilationMixer.GetChannelDilation(Channel), Duration, BlendOutDuration);
    }
}

void UTimeDilationSubsystem::ClearGlobalDilation(ETimeDilationChannel Channel) {
    if (!DilationMixer.IsActive(Channel)) {
        return;
    }

    DilationMixer.Clear(Channel);
    SyncWorldDilation();

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameTimeDilation, Log, TEXT("【时间膨胀】清除全局通道膨胀: 通道=%d"), static_cast<int32>(Channel));
    }
}

void UTimeDilationSubsystem::ClearGlobalDilationWithBlend(ETimeDilationChannel Channel, float BlendOutDuration) {
    if (!DilationMixer.IsActive(Channel)) {
        return;
    }

    if (BlendOutDuration <= 0.0f) {
        ClearGlobalDilation(Channel);
        return;
    }

    DilationMixer.ClearWithBlend(Channel, BlendOutDuration);
    SyncWorldDilation();
}

void UTimeDilationSubsystem::ClearAllGlobalDilations() {
    DilationMixer.ClearAll();
    SyncWorldDilation();
}

float UTimeDilationSubsystem::GetEffectiveGlobalTimeDilation() const {
    return DilationMixer.GetEffectiveDilation();
}

bool UTimeDilationSubsystem::IsChannelActive(ETimeDilationChannel Channel) const {
    return DilationMixer.IsActive(Channel);
}

float UTimeDilationSubsystem::GetChannelTimeDilation(ETimeDilationChannel Channel) const {
    return DilationMixer.GetChannelDilation(Channel);
}

void UTimeDilationSubsystem::AdvanceTime(float RealDeltaSeconds) {
    if (RealDeltaSeconds <= 0.0f) {
        return;
    }

    DilationMixer.Advance(RealDeltaSeconds);
    HitStopTable.Advance(RealDeltaSeconds);
    SyncWorldDilation();
}

void UTimeDilationSubsystem::SyncWorldDilation() {
    const float EffectiveDilation = DilationMixer.GetEffectiveDilation();
    if (FMath::IsNearlyEqual(LastAppliedWorldDilation, EffectiveDilation)) {
        return;
    }

    LastAppliedWorldDilation = EffectiveDilation;
    if (UWorld* World = GetWorld()) {
        if (AWorldSettings* WorldSettings = World->GetWorldSettings()) {
            WorldSettings->SetTimeDilation(EffectiveDilation);
        }
    }
}
