/**
 * @file ULoadingSubsystem.cpp
 * @brief 通用关卡流转与加载幕布子系统实现
 */

#include "System/ULoadingSubsystem.h"

#include "Config/GameCompileSwitches.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameLoading, Log, All);

ULoadingSubsystem* ULoadingSubsystem::Get(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr) {
        return nullptr;
    }

    if (const UGameInstance* AsGI = Cast<UGameInstance>(WorldContextObject)) {
        return AsGI->GetSubsystem<ULoadingSubsystem>();
    }

    if (GEngine == nullptr) {
        return nullptr;
    }

    const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (World == nullptr) {
        return nullptr;
    }

    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<ULoadingSubsystem>() : nullptr;
}

void ULoadingSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &ULoadingSubsystem::OnPreLoadMap);
    FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ULoadingSubsystem::OnPostLoadMap);
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameLoading, Log, TEXT("【加载子系统】已挂钩引擎地图加载生命周期委托。"));
    }
}

void ULoadingSubsystem::Deinitialize() {
    StopWatchdogTimer();
    FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
    FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
    ActiveBarriers.Reset();
    FinishedBarriersTelemetry.Reset();
    bMapLoadCompletedPendingBarriers = false;
    bIsLoading = false;
    TargetLevelName = NAME_None;
    CurrentLoadingProgress = 0.0f;
    OnLoadingStateChangedNative.Broadcast(false, NAME_None);
    OnLoadingStateChangedNative.Clear();
    OnLoadingProgressChangedNative.Clear();
    OnLoadingTelemetryReportNative.Clear();
    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameLoading, Log, TEXT("【加载子系统】已注销引擎地图加载生命周期委托。"));
    }
    Super::Deinitialize();
}

void ULoadingSubsystem::OpenLevelWithLoading(const FName& LevelName) {
    if (bIsLoading) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameLoading, Warning, TEXT("【加载子系统】OpenLevelWithLoading 被重复调用，当前仍处于加载中，忽略本次请求。"));
        }
        return;
    }
    if (LevelName.IsNone()) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameLoading, Warning, TEXT("【加载子系统】OpenLevelWithLoading 执行失败：目标关卡名称为空 (None)。"));
        }
        return;
    }

    UGameInstance* GameInstance = GetGameInstance();
    if (GameInstance == nullptr || GameInstance->GetWorld() == nullptr) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameLoading, Warning, TEXT("【加载子系统】OpenLevelWithLoading 执行失败：GameInstance 或世界上下文无效，未置位加载态。"));
        }
        return;
    }

    bIsLoading = true;
    bMapLoadCompletedPendingBarriers = false;
    TargetLevelName = LevelName;
    CurrentLoadingProgress = 0.0f;
    FinishedBarriersTelemetry.Reset();
    LoadingStartTimestamp = FPlatformTime::Seconds();
    MapLoadFinishedTimestamp = LoadingStartTimestamp;

    StartWatchdogTimer();

    OnLoadingStateChanged.Broadcast(true, TargetLevelName);
    OnLoadingStateChangedNative.Broadcast(true, TargetLevelName);
    RecalculateAndBroadcastProgress(FText::GetEmpty());

    UGameplayStatics::OpenLevel(GameInstance, LevelName);
}

FLoadingBarrierToken ULoadingSubsystem::AcquireBarrierToken(
    const FName& BarrierName,
    float InWeight,
    float InTimeoutSeconds
) {
    const uint64 BarrierId = ++NextBarrierId;
    FActiveBarrierInfo Info;
    Info.Name = BarrierName;
    Info.Weight = FMath::Max(0.1f, InWeight);
    Info.Progress = 0.0f;
    Info.StartTimeSeconds = FPlatformTime::Seconds();
    Info.TimeoutSeconds = InTimeoutSeconds;
    ActiveBarriers.Add(BarrierId, MoveTemp(Info));
    RecalculateAndBroadcastProgress(FText::GetEmpty());
    return FLoadingBarrierToken(this, BarrierId, BarrierName);
}

TSharedRef<FLoadingBarrierToken> ULoadingSubsystem::AcquireLoadingBarrier(
    const FName& BarrierName,
    float InWeight,
    float InTimeoutSeconds
) {
    return MakeShared<FLoadingBarrierToken>(AcquireBarrierToken(BarrierName, InWeight, InTimeoutSeconds));
}

int64 ULoadingSubsystem::AcquireLoadingBarrierBp(FName BarrierName, float InWeight, float InTimeoutSeconds) {
    const uint64 BarrierId = ++NextBarrierId;
    FActiveBarrierInfo Info;
    Info.Name = BarrierName;
    Info.Weight = FMath::Max(0.1f, InWeight);
    Info.Progress = 0.0f;
    Info.StartTimeSeconds = FPlatformTime::Seconds();
    Info.TimeoutSeconds = InTimeoutSeconds;
    ActiveBarriers.Add(BarrierId, MoveTemp(Info));
    RecalculateAndBroadcastProgress(FText::GetEmpty());
    return static_cast<int64>(BarrierId);
}

bool ULoadingSubsystem::ReleaseLoadingBarrierBp(int64 BarrierId) {
    if (BarrierId <= 0) {
        return false;
    }
    const uint64 UId = static_cast<uint64>(BarrierId);
    if (!ActiveBarriers.Contains(UId)) {
        return false;
    }
    ReleaseLoadingBarrierInternal(UId);
    return true;
}

bool ULoadingSubsystem::ReportBarrierProgressBp(int64 BarrierId, float Progress, const FText& Description) {
    if (BarrierId <= 0) {
        return false;
    }
    const uint64 UId = static_cast<uint64>(BarrierId);
    if (!ActiveBarriers.Contains(UId)) {
        return false;
    }
    UpdateBarrierProgressInternal(UId, Progress, Description);
    return true;
}

float ULoadingSubsystem::GetLoadingProgress() const {
    return CurrentLoadingProgress;
}

void ULoadingSubsystem::SetMaxLoadingTimeoutSeconds(float InTimeoutSeconds) {
    MaxLoadingTimeoutSeconds = InTimeoutSeconds;
}

float ULoadingSubsystem::GetMaxLoadingTimeoutSeconds() const {
    return MaxLoadingTimeoutSeconds;
}

bool ULoadingSubsystem::IsLoading() const {
    return bIsLoading;
}

FName ULoadingSubsystem::GetTargetLevelName() const {
    return TargetLevelName;
}

bool ULoadingSubsystem::HasActiveBarriers() const {
    return !ActiveBarriers.IsEmpty();
}

int32 ULoadingSubsystem::GetActiveBarrierCount() const {
    return ActiveBarriers.Num();
}

void ULoadingSubsystem::ReleaseLoadingBarrierInternal(uint64 BarrierId) {
    FActiveBarrierInfo Info;
    if (!ActiveBarriers.RemoveAndCopyValue(BarrierId, Info)) {
        return;
    }

    const double DurationMs = (FPlatformTime::Seconds() - Info.StartTimeSeconds) * 1000.0;
    FLoadingBarrierTelemetry Telemetry;
    Telemetry.BarrierName = Info.Name;
    Telemetry.DurationMs = static_cast<float>(DurationMs);
    Telemetry.Weight = Info.Weight;
    Telemetry.bTimedOut = false;
    FinishedBarriersTelemetry.Add(Telemetry);

    RecalculateAndBroadcastProgress(FText::GetEmpty());

    if (ActiveBarriers.IsEmpty() && bIsLoading && bMapLoadCompletedPendingBarriers) {
        FinishLoadingTransition();
    }
}

void ULoadingSubsystem::UpdateBarrierProgressInternal(uint64 BarrierId, float InProgress, const FText& InDescription) {
    if (FActiveBarrierInfo* Found = ActiveBarriers.Find(BarrierId)) {
        Found->Progress = FMath::Clamp(InProgress, 0.0f, 1.0f);
        RecalculateAndBroadcastProgress(InDescription);
    }
}

void ULoadingSubsystem::RecalculateAndBroadcastProgress(const FText& InDescription) {
    if (!bIsLoading) {
        CurrentLoadingProgress = 0.0f;
        return;
    }

    constexpr float MapWeight = 2.0f;
    const float MapProgress = bMapLoadCompletedPendingBarriers ? 1.0f : 0.0f;

    float TotalWeight = MapWeight;
    float SumWeighted = MapWeight * MapProgress;

    for (const auto& Kvp : ActiveBarriers) {
        TotalWeight += Kvp.Value.Weight;
        SumWeighted += Kvp.Value.Weight * Kvp.Value.Progress;
    }

    for (const FLoadingBarrierTelemetry& Finished : FinishedBarriersTelemetry) {
        TotalWeight += Finished.Weight;
        SumWeighted += Finished.Weight * 1.0f;
    }

    CurrentLoadingProgress = TotalWeight > 0.0f ? FMath::Clamp(SumWeighted / TotalWeight, 0.0f, 1.0f) : 1.0f;
    OnLoadingProgressChanged.Broadcast(CurrentLoadingProgress, InDescription);
    OnLoadingProgressChangedNative.Broadcast(CurrentLoadingProgress, InDescription);
}

void ULoadingSubsystem::CheckWatchdogTimeout(double CurrentTimeSeconds) {
    if (!bIsLoading) {
        StopWatchdogTimer();
        return;
    }

    const double Now = (CurrentTimeSeconds > 0.0) ? CurrentTimeSeconds : FPlatformTime::Seconds();
    const double TotalElapsed = Now - LoadingStartTimestamp;

    if (MaxLoadingTimeoutSeconds > 0.0f && TotalElapsed >= MaxLoadingTimeoutSeconds) {
        UE_LOG(LogGameLoading, Error,
            TEXT("【加载看门狗】关卡流转总耗时 (%.2f 秒) 已超出全局上限 (%.2f 秒)！强制解除当前 %d 个活跃屏障并揭开幕布！"),
            TotalElapsed, MaxLoadingTimeoutSeconds, ActiveBarriers.Num());

        for (const auto& Kvp : ActiveBarriers) {
            FLoadingBarrierTelemetry Telemetry;
            Telemetry.BarrierName = Kvp.Value.Name;
            Telemetry.DurationMs = static_cast<float>((Now - Kvp.Value.StartTimeSeconds) * 1000.0);
            Telemetry.Weight = Kvp.Value.Weight;
            Telemetry.bTimedOut = true;
            FinishedBarriersTelemetry.Add(Telemetry);
        }
        ActiveBarriers.Reset();
        FinishLoadingTransition();
        return;
    }

    TArray<uint64> TimedOutBarrierIds;
    for (const auto& Kvp : ActiveBarriers) {
        const FActiveBarrierInfo& Info = Kvp.Value;
        if (Info.TimeoutSeconds > 0.0f && (Now - Info.StartTimeSeconds) >= Info.TimeoutSeconds) {
            TimedOutBarrierIds.Add(Kvp.Key);
            UE_LOG(LogGameLoading, Error,
                TEXT("【加载看门狗】屏障 [%s] (ID=%llu) 挂起耗时 (%.2f 秒) 超出单屏障阈值 (%.2f 秒)！强制解除该屏障！"),
                *Info.Name.ToString(), Kvp.Key, (Now - Info.StartTimeSeconds), Info.TimeoutSeconds);
        }
    }

    for (const uint64 BarrierId : TimedOutBarrierIds) {
        FActiveBarrierInfo Info;
        if (ActiveBarriers.RemoveAndCopyValue(BarrierId, Info)) {
            FLoadingBarrierTelemetry Telemetry;
            Telemetry.BarrierName = Info.Name;
            Telemetry.DurationMs = static_cast<float>((Now - Info.StartTimeSeconds) * 1000.0);
            Telemetry.Weight = Info.Weight;
            Telemetry.bTimedOut = true;
            FinishedBarriersTelemetry.Add(Telemetry);
        }
    }

    if (!TimedOutBarrierIds.IsEmpty()) {
        RecalculateAndBroadcastProgress(FText::GetEmpty());
        if (ActiveBarriers.IsEmpty() && bMapLoadCompletedPendingBarriers) {
            FinishLoadingTransition();
        }
    }
}

void ULoadingSubsystem::OnWatchdogTimerTick() {
    CheckWatchdogTimeout(0.0);
}

void ULoadingSubsystem::StartWatchdogTimer() {
    StopWatchdogTimer();
    if (MaxLoadingTimeoutSeconds <= 0.0f) {
        return;
    }
    if (UGameInstance* GI = GetGameInstance()) {
        GI->GetTimerManager().SetTimer(
            WatchdogTimerHandle,
            this,
            &ULoadingSubsystem::OnWatchdogTimerTick,
            0.5f,
            true
        );
    }
}

void ULoadingSubsystem::StopWatchdogTimer() {
    if (UGameInstance* GI = GetGameInstance()) {
        GI->GetTimerManager().ClearTimer(WatchdogTimerHandle);
    }
    WatchdogTimerHandle.Invalidate();
}

void ULoadingSubsystem::OnPreLoadMap(const FString& MapName) {
    bMapLoadCompletedPendingBarriers = false;
    if (!bIsLoading) {
        bIsLoading = true;
        TargetLevelName = FName(*MapName);
        CurrentLoadingProgress = 0.0f;
        FinishedBarriersTelemetry.Reset();
        LoadingStartTimestamp = FPlatformTime::Seconds();
        MapLoadFinishedTimestamp = LoadingStartTimestamp;

        StartWatchdogTimer();

        OnLoadingStateChanged.Broadcast(true, TargetLevelName);
        OnLoadingStateChangedNative.Broadcast(true, TargetLevelName);
        RecalculateAndBroadcastProgress(FText::GetEmpty());

        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameLoading, Log, TEXT("【加载子系统】引擎触发 PreLoadMap，即将开始载入地图: %s"), *MapName);
        }
    }
}

void ULoadingSubsystem::OnPostLoadMap(UWorld* LoadedWorld) {
    if (LoadedWorld != nullptr && LoadedWorld->GetGameInstance() != nullptr && LoadedWorld->GetGameInstance() != GetGameInstance()) {
        return;
    }

    if (!bIsLoading) {
        return;
    }

    MapLoadFinishedTimestamp = FPlatformTime::Seconds();
    bMapLoadCompletedPendingBarriers = true;
    RecalculateAndBroadcastProgress(FText::GetEmpty());

    if (!ActiveBarriers.IsEmpty()) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameLoading, Log, TEXT("【加载子系统】PostLoadMap 已完成，等待 %d 个异步就绪屏障释放后再关闭幕布。"),
                ActiveBarriers.Num());
        }
        return;
    }

    FinishLoadingTransition();
}

void ULoadingSubsystem::FinishLoadingTransition() {
    StopWatchdogTimer();
    bMapLoadCompletedPendingBarriers = false;
    bIsLoading = false;
    CurrentLoadingProgress = 1.0f;
    const FName CompletedLevelName = TargetLevelName;
    TargetLevelName = NAME_None;

    const double Now = FPlatformTime::Seconds();
    FLoadingTelemetryReport Report;
    Report.LevelName = CompletedLevelName;
    Report.TotalDurationMs = static_cast<float>((Now - LoadingStartTimestamp) * 1000.0);
    Report.MapLoadDurationMs = static_cast<float>((MapLoadFinishedTimestamp - LoadingStartTimestamp) * 1000.0);
    Report.Barriers = FinishedBarriersTelemetry;

    OnLoadingProgressChanged.Broadcast(1.0f, FText::GetEmpty());
    OnLoadingProgressChangedNative.Broadcast(1.0f, FText::GetEmpty());
    OnLoadingStateChanged.Broadcast(false, CompletedLevelName);
    OnLoadingStateChangedNative.Broadcast(false, CompletedLevelName);
    OnLoadingTelemetryReportNative.Broadcast(Report);

    if constexpr (Game::Config::bEnableLog) {
        UE_LOG(LogGameLoading, Log, TEXT("【加载子系统】关卡 [%s] 加载完成！总耗时: %.2f ms (地图解析: %.2f ms，屏障数: %d)"),
            *CompletedLevelName.ToString(), Report.TotalDurationMs, Report.MapLoadDurationMs, Report.Barriers.Num());
    }
}
