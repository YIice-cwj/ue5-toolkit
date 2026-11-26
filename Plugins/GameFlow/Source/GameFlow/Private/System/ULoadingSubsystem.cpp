/**
 * @file ULoadingSubsystem.cpp
 * @brief 通用关卡流转与加载幕布子系统实现
 */

#include "System/ULoadingSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Logging/GameLogCategories.h"
#include "TimerManager.h"

namespace {

constexpr float MinBarrierWeight = 0.1f;
constexpr float DefaultBarrierWeight = 1.0f;
constexpr float MapProgressWeight = 2.0f;
constexpr float WatchdogPollIntervalSeconds = 0.5f;
constexpr int32 InlineTimedOutBarrierCapacity = 8;

/**
 * @brief 校验并标准化屏障进度加权权重（非有限值回落默认权重，有限值截断至下限）
 * @param InWeight 原始权重
 * @return 标准化后的合法正权重
 */
float SanitizeWeight(float InWeight) {
    if (!FMath::IsFinite(InWeight)) {
        GAME_LOG(Warning, TEXT("【GameFlow】屏障权重为非有限浮点值，已回落至默认权重 %.1f。"), DefaultBarrierWeight);
        return DefaultBarrierWeight;
    }
    return FMath::Max(MinBarrierWeight, InWeight);
}

/**
 * @brief 校验并标准化超时秒数（非有限值或非正数视为关闭超时返回 0.0f）
 * @param InTimeoutSeconds 原始超时秒数
 * @return 非负有限超时秒数
 */
float SanitizeTimeout(float InTimeoutSeconds) {
    if (!FMath::IsFinite(InTimeoutSeconds)) {
        GAME_LOG(Warning, TEXT("【GameFlow】超时秒数为非有限浮点值，已关闭该超时阈值。"));
        return 0.0f;
    }
    return FMath::Max(0.0f, InTimeoutSeconds);
}

/**
 * @brief 校验并截断外部上报的屏障进度值（拒绝非有限浮点输入）
 * @param InProgress 原始进度值
 * @param OutClampedProgress 输出截断至 [0.0f, 1.0f] 的有效进度
 * @return 输入合法返回 true
 */
bool TrySanitizeProgress(float InProgress, float& OutClampedProgress) {
    if (!FMath::IsFinite(InProgress)) {
        GAME_LOG(Warning, TEXT("【GameFlow】上报的屏障进度为非有限浮点值，已忽略本次更新。"));
        return false;
    }
    OutClampedProgress = FMath::Clamp(InProgress, 0.0f, 1.0f);
    return true;
}

} // namespace

ULoadingSubsystem* ULoadingSubsystem::Get(const UObject* WorldContextObject) {
    if (!::IsValid(WorldContextObject)) {
        return nullptr;
    }

    if (const ULoadingSubsystem* AsSelf = Cast<ULoadingSubsystem>(WorldContextObject)) {
        return const_cast<ULoadingSubsystem*>(AsSelf);
    }

    if (const UGameInstance* AsGameInstance = Cast<UGameInstance>(WorldContextObject)) {
        return AsGameInstance->GetSubsystem<ULoadingSubsystem>();
    }

    if (const UGameInstance* OuterGameInstance = WorldContextObject->GetTypedOuter<UGameInstance>()) {
        return OuterGameInstance->GetSubsystem<ULoadingSubsystem>();
    }

    if (GEngine == nullptr) {
        return nullptr;
    }

    const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (World == nullptr) {
        return nullptr;
    }

    const UGameInstance* GameInstance = World->GetGameInstance();
    return GameInstance != nullptr ? GameInstance->GetSubsystem<ULoadingSubsystem>() : nullptr;
}

void ULoadingSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &ULoadingSubsystem::OnPreLoadMapWithContext);
    FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ULoadingSubsystem::OnPostLoadMap);
    GAME_LOG(Verbose, TEXT("【GameFlow】加载子系统已挂钩引擎地图加载上下文委托。"));
}

void ULoadingSubsystem::Deinitialize() {
    StopWatchdogTimer();
    FCoreUObjectDelegates::PreLoadMapWithContext.RemoveAll(this);
    FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

    const bool bWasLoading = IsLoading();
    ActiveBarriers.Reset();
    FinishedBarriersTelemetry.Reset();
    FinishedBarriersTotalWeight = 0.0f;
    CustomTimeoutBarrierCount = 0;
    LoadingPhase = ELoadingTransitionPhase::Idle;
    TargetLevelName = NAME_None;
    CurrentLoadingProgress = 0.0f;
    bIsFinishingTransition = false;
    PendingNextLevelName = NAME_None;

    if (bWasLoading) {
        OnLoadingAbortedNative.Broadcast();
    }
    OnLoadingAbortedNative.Clear();
    OnLoadingStateChanged.Clear();
    OnLoadingStateChangedNative.Clear();
    OnLoadingProgressChanged.Clear();
    OnLoadingProgressChangedNative.Clear();
    OnLoadingTelemetryReport.Clear();
    OnLoadingTelemetryReportNative.Clear();

    GAME_LOG(Verbose, TEXT("【GameFlow】加载子系统已注销引擎地图加载委托并清理活跃状态。"));
    Super::Deinitialize();
}

void ULoadingSubsystem::OpenLevelWithLoading(const FName& LevelName) {
    if (IsLoading()) {
        GAME_LOG(Warning, TEXT("【GameFlow】OpenLevelWithLoading 被重复调用，当前仍处于加载中，忽略本次请求。"));
        return;
    }
    if (LevelName.IsNone()) {
        GAME_LOG(Warning, TEXT("【GameFlow】OpenLevelWithLoading 执行失败：目标关卡名称为空 (None)。"));
        return;
    }

    if (bIsFinishingTransition) {
        if (!PendingNextLevelName.IsNone()) {
            GAME_LOG(Warning, TEXT("【GameFlow】收尾广播期间已有排队目标关卡，忽略重复的 OpenLevelWithLoading 请求。"));
            return;
        }
        PendingNextLevelName = LevelName;
        return;
    }

    UGameInstance* GameInstance = GetGameInstance();
    if (GameInstance == nullptr || GameInstance->GetWorld() == nullptr) {
        GAME_LOG(Warning, TEXT("【GameFlow】OpenLevelWithLoading 执行失败：GameInstance 或世界上下文无效，未置位加载态。"));
        return;
    }

    BeginLoadingTransition(LevelName);
    UGameplayStatics::OpenLevel(GameInstance, LevelName);
}

FLoadingBarrierToken ULoadingSubsystem::AcquireBarrierToken(
    const FName& BarrierName,
    float InWeight,
    float InTimeoutSeconds
) {
    const FLoadingBarrierHandle Handle = RegisterBarrierInternal(BarrierName, InWeight, InTimeoutSeconds);
    return FLoadingBarrierToken(this, Handle, BarrierName);
}

int64 ULoadingSubsystem::AcquireLoadingBarrierBp(FName BarrierName, float InWeight, float InTimeoutSeconds) {
    const FLoadingBarrierHandle Handle = RegisterBarrierInternal(BarrierName, InWeight, InTimeoutSeconds);
    return static_cast<int64>(Handle.PackUInt64());
}

bool ULoadingSubsystem::ReleaseLoadingBarrierBp(int64 BarrierId) {
    if (BarrierId == 0) {
        return false;
    }
    const FLoadingBarrierHandle Handle = FLoadingBarrierHandle::FromPackedUInt64(static_cast<uint64>(BarrierId));
    return ReleaseLoadingBarrierInternal(Handle);
}

bool ULoadingSubsystem::ReportBarrierProgressBp(int64 BarrierId, float Progress, const FText& Description) {
    if (BarrierId == 0) {
        return false;
    }
    const FLoadingBarrierHandle Handle = FLoadingBarrierHandle::FromPackedUInt64(static_cast<uint64>(BarrierId));
    float IgnoredClamped = 0.0f;
    return UpdateBarrierProgressInternal(Handle, Progress, Description, IgnoredClamped);
}

void ULoadingSubsystem::SetMaxLoadingTimeoutSeconds(float InTimeoutSeconds) {
    MaxLoadingTimeoutSeconds = SanitizeTimeout(InTimeoutSeconds);
    SyncWatchdogTimer();
}

void ULoadingSubsystem::CheckWatchdogTimeout() {
    CheckWatchdogTimeoutAt(FPlatformTime::Seconds());
}

void ULoadingSubsystem::CheckWatchdogTimeoutAt(double CurrentTimeSeconds) {
    if (!IsLoading()) {
        StopWatchdogTimer();
        return;
    }

    const double Now = (FMath::IsFinite(CurrentTimeSeconds) && CurrentTimeSeconds > 0.0)
        ? CurrentTimeSeconds
        : FPlatformTime::Seconds();

    if (MaxLoadingTimeoutSeconds > 0.0f && (Now - LoadingStartTimestamp) >= MaxLoadingTimeoutSeconds) {
        HandleGlobalWatchdogTimeout(Now);
        return;
    }

    SweepTimedOutBarriers(Now);
}

bool ULoadingSubsystem::IsLoading() const noexcept {
    return LoadingPhase != ELoadingTransitionPhase::Idle;
}

ELoadingTransitionPhase ULoadingSubsystem::GetLoadingPhase() const noexcept {
    return LoadingPhase;
}

FName ULoadingSubsystem::GetTargetLevelName() const noexcept {
    return TargetLevelName;
}

float ULoadingSubsystem::GetLoadingProgress() const noexcept {
    return CurrentLoadingProgress;
}

float ULoadingSubsystem::GetMaxLoadingTimeoutSeconds() const noexcept {
    return MaxLoadingTimeoutSeconds;
}

bool ULoadingSubsystem::HasActiveBarriers() const noexcept {
    return !ActiveBarriers.IsEmpty();
}

int32 ULoadingSubsystem::GetActiveBarrierCount() const noexcept {
    return ActiveBarriers.Num();
}

const FLoadingTelemetryReport& ULoadingSubsystem::GetLastTelemetryReport() const noexcept {
    return LastTelemetryReport;
}

bool ULoadingSubsystem::IsBarrierActiveInternal(FLoadingBarrierHandle BarrierHandle) const noexcept {
    return ActiveBarriers.IsValidHandle(BarrierHandle);
}

FLoadingBarrierHandle ULoadingSubsystem::RegisterBarrierInternal(
    const FName& BarrierName,
    float InWeight,
    float InTimeoutSeconds
) {
    FActiveBarrierInfo Info;
    Info.Name = BarrierName;
    Info.Weight = SanitizeWeight(InWeight);
    Info.Progress = 0.0f;
    Info.TimeoutSeconds = SanitizeTimeout(InTimeoutSeconds);
    Info.StartTimeSeconds = FPlatformTime::Seconds();

    const bool bHasCustomTimeout = Info.TimeoutSeconds > 0.0f;
    const FLoadingBarrierHandle Handle = ActiveBarriers.Insert(MoveTemp(Info));

    if (bHasCustomTimeout) {
        ++CustomTimeoutBarrierCount;
        SyncWatchdogTimer();
    }

    if (IsLoading()) {
        RecalculateAndBroadcastProgress(FText::GetEmpty());
    }
    return Handle;
}

bool ULoadingSubsystem::ReleaseLoadingBarrierInternal(FLoadingBarrierHandle BarrierHandle) {
    const FActiveBarrierInfo* Found = ActiveBarriers.Find(BarrierHandle);
    if (Found == nullptr) {
        return false;
    }

    const FActiveBarrierInfo RemovedInfo = *Found;
    ActiveBarriers.Remove(BarrierHandle);

    if (RemovedInfo.TimeoutSeconds > 0.0f) {
        --CustomTimeoutBarrierCount;
        if (CustomTimeoutBarrierCount == 0) {
            SyncWatchdogTimer();
        }
    }

    if (!IsLoading()) {
        return true;
    }

    const double Now = FPlatformTime::Seconds();
    RecordBarrierTelemetry(RemovedInfo, Now, false);

    if (ActiveBarriers.IsEmpty() && LoadingPhase == ELoadingTransitionPhase::WaitingBarriers) {
        FinishLoadingTransition(false, Now);
        return true;
    }

    RecalculateAndBroadcastProgress(FText::GetEmpty());
    return true;
}

bool ULoadingSubsystem::UpdateBarrierProgressInternal(
    FLoadingBarrierHandle BarrierHandle,
    float InProgress,
    const FText& InDescription,
    float& OutClampedProgress
) {
    FActiveBarrierInfo* Found = ActiveBarriers.Find(BarrierHandle);
    if (Found == nullptr) {
        return false;
    }
    if (!TrySanitizeProgress(InProgress, OutClampedProgress)) {
        return false;
    }

    Found->Progress = OutClampedProgress;
    if (IsLoading()) {
        RecalculateAndBroadcastProgress(InDescription);
    }
    return true;
}

void ULoadingSubsystem::BeginLoadingTransition(const FName& InLevelName) {
    LoadingPhase = ELoadingTransitionPhase::LoadingMap;
    TargetLevelName = InLevelName;
    CurrentLoadingProgress = 0.0f;
    FinishedBarriersTelemetry.Reset();
    FinishedBarriersTotalWeight = 0.0f;
    LoadingStartTimestamp = FPlatformTime::Seconds();
    MapLoadFinishedTimestamp = LoadingStartTimestamp;

    SyncWatchdogTimer();

    OnLoadingStateChanged.Broadcast(true, TargetLevelName);
    OnLoadingStateChangedNative.Broadcast(true, TargetLevelName);
    RecalculateAndBroadcastProgress(FText::GetEmpty());
}

void ULoadingSubsystem::FinishLoadingTransition(bool bTimedOut, double CompletionTimestamp) {
    StopWatchdogTimer();

    const bool bMapLoadedBeforeFinish = (LoadingPhase == ELoadingTransitionPhase::WaitingBarriers);
    LoadingPhase = ELoadingTransitionPhase::Idle;
    CurrentLoadingProgress = 1.0f;
    const FName CompletedLevelName = TargetLevelName;
    TargetLevelName = NAME_None;

    FLoadingTelemetryReport Report;
    Report.LevelName = CompletedLevelName;
    Report.TotalDurationMs = static_cast<float>(FMath::Max(0.0, (CompletionTimestamp - LoadingStartTimestamp) * 1000.0));
    Report.MapLoadDurationMs = bMapLoadedBeforeFinish
        ? static_cast<float>(FMath::Max(0.0, (MapLoadFinishedTimestamp - LoadingStartTimestamp) * 1000.0))
        : Report.TotalDurationMs;
    Report.bTimedOut = bTimedOut;
    Report.Barriers = MoveTemp(FinishedBarriersTelemetry);
    FinishedBarriersTelemetry.Reset();
    FinishedBarriersTotalWeight = 0.0f;
    LastTelemetryReport = Report;

    bIsFinishingTransition = true;
    PendingNextLevelName = NAME_None;

    OnLoadingProgressChanged.Broadcast(1.0f, FText::GetEmpty());
    OnLoadingProgressChangedNative.Broadcast(1.0f, FText::GetEmpty());
    OnLoadingStateChanged.Broadcast(false, CompletedLevelName);
    OnLoadingStateChangedNative.Broadcast(false, CompletedLevelName);
    OnLoadingTelemetryReport.Broadcast(Report);
    OnLoadingTelemetryReportNative.Broadcast(Report);

    GAME_LOG(
        Log,
        TEXT("【GameFlow】关卡 [%s] 加载结束 (超时=%s)！总耗时: %.2f ms (地图阶段: %.2f ms，屏障数: %d)"),
        *CompletedLevelName.ToString(),
        bTimedOut ? TEXT("true") : TEXT("false"),
        Report.TotalDurationMs,
        Report.MapLoadDurationMs,
        Report.Barriers.Num()
    );

    bIsFinishingTransition = false;
    if (!PendingNextLevelName.IsNone()) {
        const FName DeferredLevelName = PendingNextLevelName;
        PendingNextLevelName = NAME_None;
        OpenLevelWithLoading(DeferredLevelName);
    }
}

void ULoadingSubsystem::RecalculateAndBroadcastProgress(const FText& InDescription) {
    if (!IsLoading()) {
        CurrentLoadingProgress = 0.0f;
        return;
    }

    const float MapProgress = (LoadingPhase == ELoadingTransitionPhase::WaitingBarriers) ? 1.0f : 0.0f;
    float TotalWeight = MapProgressWeight + FinishedBarriersTotalWeight;
    float SumWeighted = (MapProgressWeight * MapProgress) + FinishedBarriersTotalWeight;

    for (const FActiveBarrierInfo& Info : ActiveBarriers.GetDenseValues()) {
        TotalWeight += Info.Weight;
        SumWeighted += Info.Weight * Info.Progress;
    }

    CurrentLoadingProgress = TotalWeight > 0.0f ? FMath::Clamp(SumWeighted / TotalWeight, 0.0f, 1.0f) : 1.0f;

    if (OnLoadingProgressChanged.IsBound()) {
        OnLoadingProgressChanged.Broadcast(CurrentLoadingProgress, InDescription);
    }
    if (OnLoadingProgressChangedNative.IsBound()) {
        OnLoadingProgressChangedNative.Broadcast(CurrentLoadingProgress, InDescription);
    }
}

void ULoadingSubsystem::RecordBarrierTelemetry(const FActiveBarrierInfo& Info, double NowSeconds, bool bTimedOut) {
    FLoadingBarrierTelemetry Telemetry;
    Telemetry.BarrierName = Info.Name;
    Telemetry.DurationMs = static_cast<float>(FMath::Max(0.0, (NowSeconds - Info.StartTimeSeconds) * 1000.0));
    Telemetry.Weight = Info.Weight;
    Telemetry.bTimedOut = bTimedOut;
    FinishedBarriersTelemetry.Add(Telemetry);
    FinishedBarriersTotalWeight += Info.Weight;
}

void ULoadingSubsystem::HandleGlobalWatchdogTimeout(double NowSeconds) {
    const double TotalElapsed = NowSeconds - LoadingStartTimestamp;
    GAME_LOG(
        Error,
        TEXT("【加载看门狗】关卡流转总耗时 (%.2f 秒) 已超出全局上限 (%.2f 秒)！强制解除当前 %d 个活跃屏障并揭开幕布！"),
        TotalElapsed,
        MaxLoadingTimeoutSeconds,
        ActiveBarriers.Num()
    );

    for (const FActiveBarrierInfo& Info : ActiveBarriers.GetDenseValues()) {
        RecordBarrierTelemetry(Info, NowSeconds, true);
    }
    ActiveBarriers.Reset();
    CustomTimeoutBarrierCount = 0;
    FinishLoadingTransition(true, NowSeconds);
}

void ULoadingSubsystem::SweepTimedOutBarriers(double NowSeconds) {
    if (CustomTimeoutBarrierCount <= 0) {
        return;
    }

    TArray<FLoadingBarrierHandle, TInlineAllocator<InlineTimedOutBarrierCapacity>> TimedOutHandles;
    ActiveBarriers.ForEach([&TimedOutHandles, NowSeconds](FLoadingBarrierHandle Handle, const FActiveBarrierInfo& Info) {
        if (Info.TimeoutSeconds > 0.0f && (NowSeconds - Info.StartTimeSeconds) >= Info.TimeoutSeconds) {
            TimedOutHandles.Add(Handle);
            GAME_LOG(
                Error,
                TEXT("【加载看门狗】屏障 [%s] (Handle=%llu) 挂起耗时 (%.2f 秒) 超出单屏障阈值 (%.2f 秒)！强制解除该屏障！"),
                *Info.Name.ToString(),
                Handle.PackUInt64(),
                NowSeconds - Info.StartTimeSeconds,
                Info.TimeoutSeconds
            );
        }
    });

    if (TimedOutHandles.IsEmpty()) {
        return;
    }

    for (const FLoadingBarrierHandle Handle : TimedOutHandles) {
        const FActiveBarrierInfo* Found = ActiveBarriers.Find(Handle);
        if (Found == nullptr) {
            continue;
        }
        const FActiveBarrierInfo RemovedInfo = *Found;
        ActiveBarriers.Remove(Handle);
        if (RemovedInfo.TimeoutSeconds > 0.0f) {
            --CustomTimeoutBarrierCount;
        }
        RecordBarrierTelemetry(RemovedInfo, NowSeconds, true);
    }

    SyncWatchdogTimer();

    if (ActiveBarriers.IsEmpty() && LoadingPhase == ELoadingTransitionPhase::WaitingBarriers) {
        FinishLoadingTransition(false, NowSeconds);
        return;
    }

    RecalculateAndBroadcastProgress(FText::GetEmpty());
}

bool ULoadingSubsystem::ShouldRunWatchdogTimer() const noexcept {
    return IsLoading() && (MaxLoadingTimeoutSeconds > 0.0f || CustomTimeoutBarrierCount > 0);
}

void ULoadingSubsystem::SyncWatchdogTimer() {
    if (!ShouldRunWatchdogTimer()) {
        StopWatchdogTimer();
        return;
    }

    UGameInstance* GameInstance = GetGameInstance();
    if (GameInstance == nullptr) {
        return;
    }

    FTimerManager& TimerManager = GameInstance->GetTimerManager();
    if (!TimerManager.IsTimerActive(WatchdogTimerHandle)) {
        TimerManager.SetTimer(
            WatchdogTimerHandle,
            this,
            &ULoadingSubsystem::OnWatchdogTimerTick,
            WatchdogPollIntervalSeconds,
            true
        );
    }
}

void ULoadingSubsystem::StopWatchdogTimer() {
    if (!WatchdogTimerHandle.IsValid()) {
        return;
    }
    if (UGameInstance* GameInstance = GetGameInstance()) {
        GameInstance->GetTimerManager().ClearTimer(WatchdogTimerHandle);
    }
    WatchdogTimerHandle.Invalidate();
}

void ULoadingSubsystem::OnWatchdogTimerTick() {
    CheckWatchdogTimeout();
}

void ULoadingSubsystem::OnPreLoadMapWithContext(const FWorldContext& WorldContext, const FString& MapName) {
    if (WorldContext.OwningGameInstance != GetGameInstance()) {
        return;
    }

    if (LoadingPhase == ELoadingTransitionPhase::LoadingMap) {
        return;
    }

    GAME_LOG(Log, TEXT("【GameFlow】引擎触发 PreLoadMapWithContext，即将开始载入地图: %s"), *MapName);
    BeginLoadingTransition(FName(*MapName));
}

void ULoadingSubsystem::OnPostLoadMap(UWorld* LoadedWorld) {
    if (LoadedWorld != nullptr && LoadedWorld->GetGameInstance() != GetGameInstance()) {
        return;
    }

    if (!IsLoading()) {
        return;
    }

    if (LoadedWorld == nullptr) {
        GAME_LOG(Warning, TEXT("【GameFlow】PostLoadMapWithWorld 收到空世界指针（地图加载失败兜底），按当前屏障状态推进收尾流程。"));
    }

    MapLoadFinishedTimestamp = FPlatformTime::Seconds();
    LoadingPhase = ELoadingTransitionPhase::WaitingBarriers;

    if (ActiveBarriers.IsEmpty()) {
        FinishLoadingTransition(false, MapLoadFinishedTimestamp);
        return;
    }

    RecalculateAndBroadcastProgress(FText::GetEmpty());
    GAME_LOG(
        Log,
        TEXT("【GameFlow】PostLoadMap 已完成，等待 %d 个异步就绪屏障释放后再关闭幕布。"),
        ActiveBarriers.Num()
    );
}
