/**
 * @file UThrottledTickerSubsystem.cpp
 * @brief 分级交错节流心跳调度子系统与句柄实现
 */

#include "Tasks/UThrottledTickerSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

FThrottledTickHandle::FThrottledTickHandle(UThrottledTickerSubsystem* InSubsystem, uint64 InHandleId) noexcept
    : SubsystemWeak(InSubsystem)
    , HandleId(InHandleId) {}

void FThrottledTickHandle::Unregister() {
    if (UThrottledTickerSubsystem* Subsystem = SubsystemWeak.Get()) {
        Subsystem->UnregisterTick(HandleId);
        HandleId = 0ULL;
    }
}

void FThrottledTickHandle::SetRate(EThrottledRate NewRate) {
    if (UThrottledTickerSubsystem* Subsystem = SubsystemWeak.Get()) {
        Subsystem->SetTickRate(HandleId, NewRate);
    }
}

void FThrottledTickHandle::SetInterval(float NewIntervalSeconds) {
    if (UThrottledTickerSubsystem* Subsystem = SubsystemWeak.Get()) {
        Subsystem->SetTickInterval(HandleId, NewIntervalSeconds);
    }
}

bool FThrottledTickHandle::IsActive() const {
    if (UThrottledTickerSubsystem* Subsystem = SubsystemWeak.Get()) {
        return Subsystem->IsTickActive(HandleId);
    }
    return false;
}

void UThrottledTickerSubsystem::Deinitialize() {
    ClearAll();
    Super::Deinitialize();
}

TStatId UThrottledTickerSubsystem::GetStatId() const {
    RETURN_QUICK_DECLARE_CYCLE_STAT(UThrottledTickerSubsystem, STATGROUP_Tickables);
}

bool UThrottledTickerSubsystem::IsTickable() const {
    return !HasAnyFlags(RF_ClassDefaultObject) && !Entries.IsEmpty();
}

float UThrottledTickerSubsystem::RateToIntervalSeconds(EThrottledRate Rate) noexcept {
    switch (Rate) {
        case EThrottledRate::EveryFrame:
            return 0.0f;
        case EThrottledRate::Rate30Hz:
            return 1.0f / 30.0f;
        case EThrottledRate::Rate10Hz:
            return 0.1f;
        case EThrottledRate::Rate2Hz:
            return 0.5f;
        case EThrottledRate::Rate1Hz:
            return 1.0f;
        default:
            return 0.0f;
    }
}

UThrottledTickerSubsystem& UThrottledTickerSubsystem::Get(const UObject* WorldContextObject) {
    check(WorldContextObject != nullptr);
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::Assert);
    check(World != nullptr);
    UThrottledTickerSubsystem* Subsystem = World->GetSubsystem<UThrottledTickerSubsystem>();
    check(Subsystem != nullptr);
    return *Subsystem;
}

UThrottledTickerSubsystem* UThrottledTickerSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr || GEngine == nullptr) {
        return nullptr;
    }
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
        return World->GetSubsystem<UThrottledTickerSubsystem>();
    }
    return nullptr;
}

FThrottledTickHandle UThrottledTickerSubsystem::RegisterTick(
    TWeakObjectPtr<UObject> Owner,
    EThrottledRate Rate,
    FThrottledTickCallback Callback
) {
    return RegisterIntervalTick(MoveTemp(Owner), RateToIntervalSeconds(Rate), MoveTemp(Callback));
}

FThrottledTickHandle UThrottledTickerSubsystem::RegisterIntervalTick(
    TWeakObjectPtr<UObject> Owner,
    float IntervalSeconds,
    FThrottledTickCallback Callback
) {
    if (!Callback) {
        return FThrottledTickHandle();
    }

    const uint64 AllocatedId = NextHandleId++;
    FThrottledEntry NewEntry;
    NewEntry.HandleId = AllocatedId;
    NewEntry.Owner = MoveTemp(Owner);
    NewEntry.Callback = MoveTemp(Callback);
    NewEntry.Interval = IntervalSeconds > 0.0f ? IntervalSeconds : 0.0f;
    NewEntry.bHasOwner = NewEntry.Owner.Get(true) != nullptr;
    NewEntry.bPendingRemoval = false;

    if (NewEntry.Interval > 0.0f) {
        NewEntry.Elapsed = static_cast<float>(AllocatedId % 8ULL) * (NewEntry.Interval / 8.0f);
    } else {
        NewEntry.Elapsed = 0.0f;
    }

    Entries.Add(MoveTemp(NewEntry));
    return FThrottledTickHandle(this, AllocatedId);
}

bool UThrottledTickerSubsystem::UnregisterTick(uint64 HandleId) {
    if (HandleId == 0ULL) {
        return false;
    }

    for (FThrottledEntry& Entry : Entries) {
        if (Entry.HandleId == HandleId && !Entry.bPendingRemoval) {
            Entry.bPendingRemoval = true;
            if (!bIsIterating) {
                Entries.RemoveAll([](const FThrottledEntry& E) { return E.bPendingRemoval; });
            }
            return true;
        }
    }
    return false;
}

bool UThrottledTickerSubsystem::SetTickRate(uint64 HandleId, EThrottledRate NewRate) {
    return SetTickInterval(HandleId, RateToIntervalSeconds(NewRate));
}

bool UThrottledTickerSubsystem::SetTickInterval(uint64 HandleId, float NewIntervalSeconds) {
    if (HandleId == 0ULL) {
        return false;
    }

    for (FThrottledEntry& Entry : Entries) {
        if (Entry.HandleId == HandleId && !Entry.bPendingRemoval) {
            Entry.Interval = NewIntervalSeconds > 0.0f ? NewIntervalSeconds : 0.0f;
            Entry.Elapsed = 0.0f;
            return true;
        }
    }
    return false;
}

bool UThrottledTickerSubsystem::IsTickActive(uint64 HandleId) const {
    if (HandleId == 0ULL) {
        return false;
    }

    for (const FThrottledEntry& Entry : Entries) {
        if (Entry.HandleId == HandleId && !Entry.bPendingRemoval) {
            if (Entry.bHasOwner && !Entry.Owner.IsValid()) {
                return false;
            }
            return true;
        }
    }
    return false;
}

int32 UThrottledTickerSubsystem::GetRegisteredCount() const {
    int32 ActiveCount = 0;
    for (const FThrottledEntry& Entry : Entries) {
        if (!Entry.bPendingRemoval) {
            if (!Entry.bHasOwner || Entry.Owner.IsValid()) {
                ++ActiveCount;
            }
        }
    }
    return ActiveCount;
}

void UThrottledTickerSubsystem::ClearAll() {
    Entries.Empty();
}

void UThrottledTickerSubsystem::Tick(float DeltaTime) {
    if (Entries.IsEmpty()) {
        return;
    }

    bIsIterating = true;
    const int32 NumToTick = Entries.Num();

    for (int32 Index = 0; Index < NumToTick; ++Index) {
        FThrottledEntry& Entry = Entries[Index];
        if (Entry.bPendingRemoval) {
            continue;
        }

        if (Entry.bHasOwner && !Entry.Owner.IsValid()) {
            Entry.bPendingRemoval = true;
            continue;
        }

        if (Entry.Interval <= 0.0f) {
            Entry.Callback(DeltaTime);
        } else {
            Entry.Elapsed += DeltaTime;
            if (Entry.Elapsed >= Entry.Interval) {
                const float FiredDelta = Entry.Elapsed;
                Entry.Elapsed = 0.0f;
                Entry.Callback(FiredDelta);
            }
        }
    }

    bIsIterating = false;

    Entries.RemoveAll([](const FThrottledEntry& E) {
        return E.bPendingRemoval || (E.bHasOwner && !E.Owner.IsValid());
    });
}
