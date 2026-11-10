/**
 * @file FThrottledTickScheduler.cpp
 * @brief 纯 C++ 分级交错节流心跳调度器实现
 */

#include "Async/FThrottledTickScheduler.h"

void FThrottledTickScheduler::Tick(float DeltaTime) {
    if (Entries.IsEmpty() || bIsIterating) {
        return;
    }

    bIsIterating = true;
    const int32 InitialCount = Entries.Num();
    for (int32 Index = 0; Index < InitialCount && Index < Entries.Num(); ++Index) {
        if (Entries[Index].bPendingRemoval) {
            continue;
        }

        if (Entries[Index].bHasOwner && Entries[Index].OwnerValidator && !Entries[Index].OwnerValidator()) {
            Entries[Index].bPendingRemoval = true;
            continue;
        }

        if (Entries[Index].Interval <= 0.0f) {
            FThrottledTickCallback CallbackCopy = Entries[Index].Callback;
            if (CallbackCopy) {
                CallbackCopy(DeltaTime);
            }
            continue;
        }

        Entries[Index].Elapsed += DeltaTime;
        if (Entries[Index].Elapsed >= Entries[Index].Interval) {
            const float ActualElapsed = Entries[Index].Elapsed;
            Entries[Index].Elapsed = FMath::Fmod(Entries[Index].Elapsed, Entries[Index].Interval);
            FThrottledTickCallback CallbackCopy = Entries[Index].Callback;
            if (CallbackCopy) {
                CallbackCopy(ActualElapsed);
            }
        }
    }
    bIsIterating = false;

    Entries.RemoveAll([](const FThrottledEntry& E) {
        return E.bPendingRemoval;
    });
}

uint64 FThrottledTickScheduler::RegisterIntervalTick(
    float IntervalSeconds,
    FThrottledTickCallback Callback) {
    return RegisterIntervalTick(FOwnerValidator(nullptr), IntervalSeconds, MoveTemp(Callback));
}

uint64 FThrottledTickScheduler::RegisterIntervalTick(
    FOwnerValidator InValidator,
    float IntervalSeconds,
    FThrottledTickCallback Callback) {
    if (!Callback) {
        return 0ULL;
    }

    const uint64 AllocatedId = NextHandleId++;
    FThrottledEntry NewEntry;
    NewEntry.HandleId = AllocatedId;
    NewEntry.OwnerValidator = MoveTemp(InValidator);
    NewEntry.Callback = MoveTemp(Callback);
    NewEntry.Interval = IntervalSeconds > 0.0f ? IntervalSeconds : 0.0f;
    NewEntry.bHasOwner = (NewEntry.OwnerValidator != nullptr);
    NewEntry.bPendingRemoval = false;

    if (NewEntry.Interval > 0.0f) {
        NewEntry.Elapsed = static_cast<float>(AllocatedId % 8ULL) * (NewEntry.Interval / 8.0f);
    } else {
        NewEntry.Elapsed = 0.0f;
    }

    Entries.Add(MoveTemp(NewEntry));
    return AllocatedId;
}

uint64 FThrottledTickScheduler::RegisterIntervalTick(
    TWeakObjectPtr<UObject> Owner,
    float IntervalSeconds,
    FThrottledTickCallback Callback) {
    if (!Callback) {
        return 0ULL;
    }
    if (Owner.IsExplicitlyNull()) {
        return RegisterIntervalTick(IntervalSeconds, MoveTemp(Callback));
    }
    if (!Owner.IsValid()) {
        return 0ULL;
    }
    return RegisterIntervalTick(
        FOwnerValidator([Owner]() -> bool {
            return Owner.IsValid();
        }),
        IntervalSeconds,
        MoveTemp(Callback)
    );
}

bool FThrottledTickScheduler::UnregisterTick(uint64 HandleId) {
    if (HandleId == 0ULL) {
        return false;
    }

    for (FThrottledEntry& Entry : Entries) {
        if (Entry.HandleId == HandleId && !Entry.bPendingRemoval) {
            Entry.bPendingRemoval = true;
            if (!bIsIterating) {
                Entries.RemoveAll([](const FThrottledEntry& E) {
                    return E.bPendingRemoval;
                });
            }
            return true;
        }
    }
    return false;
}

bool FThrottledTickScheduler::SetTickInterval(uint64 HandleId, float NewIntervalSeconds) {
    if (HandleId == 0ULL) {
        return false;
    }

    const float NormalizedInterval = NewIntervalSeconds > 0.0f ? NewIntervalSeconds : 0.0f;
    for (FThrottledEntry& Entry : Entries) {
        if (Entry.HandleId == HandleId && !Entry.bPendingRemoval) {
            if (FMath::Abs(Entry.Interval - NormalizedInterval) <= KINDA_SMALL_NUMBER) {
                Entry.Elapsed = 0.0f;
            } else {
                Entry.Interval = NormalizedInterval;
                if (NormalizedInterval <= 0.0f) {
                    Entry.Elapsed = 0.0f;
                } else if (Entry.Elapsed >= NormalizedInterval) {
                    Entry.Elapsed = NormalizedInterval;
                }
            }
            return true;
        }
    }
    return false;
}

bool FThrottledTickScheduler::IsTickActive(uint64 HandleId) const {
    if (HandleId == 0ULL) {
        return false;
    }

    for (const FThrottledEntry& Entry : Entries) {
        if (Entry.HandleId == HandleId && !Entry.bPendingRemoval) {
            if (Entry.bHasOwner && Entry.OwnerValidator && !Entry.OwnerValidator()) {
                return false;
            }
            return true;
        }
    }
    return false;
}

int32 FThrottledTickScheduler::GetRegisteredCount() const {
    int32 ActiveCount = 0;
    for (const FThrottledEntry& Entry : Entries) {
        if (!Entry.bPendingRemoval) {
            ++ActiveCount;
        }
    }
    return ActiveCount;
}

void FThrottledTickScheduler::ClearAll() {
    if (bIsIterating) {
        for (FThrottledEntry& Entry : Entries) {
            Entry.bPendingRemoval = true;
        }
        return;
    }
    Entries.Empty();
}

float FThrottledTickScheduler::RateToIntervalSeconds(EThrottledRate Rate) noexcept {
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
