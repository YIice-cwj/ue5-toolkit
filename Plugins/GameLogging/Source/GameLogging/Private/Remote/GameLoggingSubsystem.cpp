// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameLoggingSubsystem.cpp
 * @brief 游戏实例级远程日志常驻子系统实现
 */

#include "Remote/GameLoggingSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Message/GameplayMessageSubsystem.h"
#include "Remote/FRemoteLogReentrancyScope.h"

UGameLoggingSubsystem::~UGameLoggingSubsystem() {
    StopCapture();
}

UGameLoggingSubsystem& UGameLoggingSubsystem::Get(const UObject* WorldContextObject) {
    UGameLoggingSubsystem* Subsystem = GetPtr(WorldContextObject);
    checkf(
        Subsystem != nullptr,
        TEXT("UGameLoggingSubsystem::Get failed: WorldContextObject '%s' has no valid GameInstance."),
        *GetNameSafe(WorldContextObject)
    );
    return *Subsystem;
}

UGameLoggingSubsystem* UGameLoggingSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr) {
        return nullptr;
    }
    if (const UGameInstance* GameInstance = Cast<UGameInstance>(WorldContextObject)) {
        return GameInstance->GetSubsystem<UGameLoggingSubsystem>();
    }
    if (const UGameInstanceSubsystem* OtherSubsystem = Cast<UGameInstanceSubsystem>(WorldContextObject)) {
        const UGameInstance* GameInstance = OtherSubsystem->GetGameInstance();
        return (GameInstance != nullptr) ? GameInstance->GetSubsystem<UGameLoggingSubsystem>() : nullptr;
    }
    if (GEngine == nullptr) {
        return nullptr;
    }
    const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    const UGameInstance* GameInstance = (World != nullptr) ? World->GetGameInstance() : nullptr;
    return (GameInstance != nullptr) ? GameInstance->GetSubsystem<UGameLoggingSubsystem>() : nullptr;
}

void UGameLoggingSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    Collection.InitializeDependency<UGameplayMessageSubsystem>();

    OutputDevice = MakeUnique<FGameRemoteLogOutputDevice>();
    OutputDevice->SetMinVerbosity(DesiredMinVerbosity);
    OutputDevice->SetMaxCapacity(DesiredMaxQueueCapacity);
    bInitialized = true;

    if (bRemoteLoggingEnabled) {
        StartCapture();
    }
}

void UGameLoggingSubsystem::Deinitialize() {
    StopCapture();
    FlushQueue();
    bInitialized = false;
    ReusableBatch.Empty();
    OutputDevice.Reset();

    Super::Deinitialize();
}

void UGameLoggingSubsystem::SetRemoteLoggingEnabled(bool bEnabled) {
    check(IsInGameThread());
    if (bRemoteLoggingEnabled == bEnabled) {
        return;
    }

    bRemoteLoggingEnabled = bEnabled;
    if (!bInitialized) {
        return;
    }

    if (bRemoteLoggingEnabled) {
        StartCapture();
    } else {
        StopCapture();
    }
}

bool UGameLoggingSubsystem::IsRemoteLoggingEnabled() const noexcept {
    return bRemoteLoggingEnabled;
}

void UGameLoggingSubsystem::SetMinVerbosity(ELogVerbosity::Type InMinVerbosity) noexcept {
    DesiredMinVerbosity = static_cast<ELogVerbosity::Type>(InMinVerbosity & ELogVerbosity::VerbosityMask);
    if (OutputDevice.IsValid()) {
        OutputDevice->SetMinVerbosity(DesiredMinVerbosity);
    }
}

ELogVerbosity::Type UGameLoggingSubsystem::GetMinVerbosity() const noexcept {
    if (OutputDevice.IsValid()) {
        return OutputDevice->GetMinVerbosity();
    }
    return DesiredMinVerbosity;
}

void UGameLoggingSubsystem::SetMaxDrainBatchPerTick(int32 InMaxDrain) noexcept {
    if (InMaxDrain > 0) {
        MaxDrainBatchPerTick = InMaxDrain;
    }
}

int32 UGameLoggingSubsystem::GetMaxDrainBatchPerTick() const noexcept {
    return MaxDrainBatchPerTick;
}

void UGameLoggingSubsystem::SetMaxQueueCapacity(int32 InCapacity) noexcept {
    if (InCapacity <= 0) {
        return;
    }
    DesiredMaxQueueCapacity = FMath::Min(InCapacity, FGameRemoteLogOutputDevice::MAX_CAPACITY);
    if (OutputDevice.IsValid()) {
        OutputDevice->SetMaxCapacity(DesiredMaxQueueCapacity);
    }
}

int32 UGameLoggingSubsystem::GetMaxQueueCapacity() const noexcept {
    if (OutputDevice.IsValid()) {
        return OutputDevice->GetMaxCapacity();
    }
    return DesiredMaxQueueCapacity;
}

void UGameLoggingSubsystem::AddIgnoredCategory(FName CategoryName) {
    if (OutputDevice.IsValid()) {
        OutputDevice->AddIgnoredCategory(CategoryName);
    }
}

void UGameLoggingSubsystem::RemoveIgnoredCategory(FName CategoryName) {
    if (OutputDevice.IsValid()) {
        OutputDevice->RemoveIgnoredCategory(CategoryName);
    }
}

bool UGameLoggingSubsystem::IsCategoryIgnored(FName CategoryName) const {
    return OutputDevice.IsValid() && OutputDevice->IsCategoryIgnored(CategoryName);
}

int32 UGameLoggingSubsystem::GetQueuedCount() const noexcept {
    return OutputDevice.IsValid() ? OutputDevice->GetQueuedCount() : 0;
}

int64 UGameLoggingSubsystem::GetDroppedCount() const noexcept {
    return OutputDevice.IsValid() ? OutputDevice->GetDroppedCount() : 0;
}

int32 UGameLoggingSubsystem::FlushQueue() {
    check(IsInGameThread());
    if (!OutputDevice.IsValid()) {
        return 0;
    }

    const int32 FlushBudget = FMath::Max(OutputDevice->GetMaxCapacity(), OutputDevice->GetQueuedCount());
    int32 TotalFlushed = 0;
    while (TotalFlushed < FlushBudget) {
        const int32 BatchSize = FMath::Min(MaxDrainBatchPerTick, FlushBudget - TotalFlushed);
        const int32 Drained = DrainAndBroadcast(BatchSize);
        if (Drained <= 0) {
            break;
        }
        TotalFlushed += Drained;
    }

    return TotalFlushed;
}

void UGameLoggingSubsystem::ClearQueue() {
    if (OutputDevice.IsValid()) {
        OutputDevice->Clear();
    }
}

int32 UGameLoggingSubsystem::DrainAndBroadcast(int32 MaxCount) {
    check(IsInGameThread());
    if (!OutputDevice.IsValid() || MaxCount <= 0 || OutputDevice->IsEmpty()) {
        return 0;
    }
    if (Game::Log::Private::FRemoteLogReentrancyScope::IsHandling()) {
        return 0;
    }

    UGameplayMessageSubsystem* MessageBus = UGameplayMessageSubsystem::GetPtr(this);
    if (MessageBus == nullptr || !MessageBus->HasListener(TAG_Game_Log_Remote)) {
        return OutputDevice->DiscardBatch(MaxCount);
    }

    ReusableBatch.Reset();
    const int32 Drained = OutputDevice->DrainBatch(ReusableBatch, MaxCount);
    if (Drained <= 0) {
        return 0;
    }

    const Game::Log::Private::FRemoteLogReentrancyScope BroadcastReentrancyGuard;
    for (const FGameRemoteLogEntry& Entry : ReusableBatch) {
        MessageBus->BroadcastMessage(TAG_Game_Log_Remote, Entry);
    }
    ReusableBatch.Reset();

    return Drained;
}

bool UGameLoggingSubsystem::HandleTicker([[maybe_unused]] float DeltaTime) {
    DrainAndBroadcast(MaxDrainBatchPerTick);
    return true;
}

void UGameLoggingSubsystem::StartCapture() {
    if (GLog != nullptr && OutputDevice.IsValid() && !bDeviceAttached) {
        GLog->AddOutputDevice(OutputDevice.Get());
        bDeviceAttached = true;
    }
    if (!TickerHandle.IsValid()) {
        TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateUObject(this, &UGameLoggingSubsystem::HandleTicker),
            0.0f
        );
    }
}

void UGameLoggingSubsystem::StopCapture() {
    if (TickerHandle.IsValid()) {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }
    if (GLog != nullptr && OutputDevice.IsValid() && bDeviceAttached) {
        GLog->RemoveOutputDevice(OutputDevice.Get());
        bDeviceAttached = false;
    }
}
