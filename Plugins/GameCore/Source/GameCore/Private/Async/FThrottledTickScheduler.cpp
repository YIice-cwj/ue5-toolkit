/**
 * @file FThrottledTickScheduler.cpp
 * @brief 纯 C++ 分级交错节流心跳调度器实现
 */

#include "Async/FThrottledTickScheduler.h"

/**
 * @brief 推进调度器中心跳时间
 * @param DeltaTime 帧流逝时间
 */
void FThrottledTickScheduler::Tick(float DeltaTime) {
    checkf(IsInGameThread(), TEXT("FThrottledTickScheduler::Tick must be called on GameThread"));

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

/**
 * @brief 注册无宿主绑定的全局节流心跳
 * @param IntervalSeconds 间隔秒数
 * @param Callback 心跳回调闭包
 * @return 分配的句柄 ID
 */
uint64 FThrottledTickScheduler::RegisterIntervalTick(
    float IntervalSeconds,
    FThrottledTickCallback Callback) {
    checkf(IsInGameThread(), TEXT("FThrottledTickScheduler::RegisterIntervalTick must be called on GameThread"));
    return RegisterIntervalTick(FOwnerValidator(nullptr), IntervalSeconds, MoveTemp(Callback));
}

/**
 * @brief 注册自定义秒级间隔节流心跳（通用校验器闭包）
 * @param InValidator 宿主生命周期有效性校验器
 * @param IntervalSeconds 间隔秒数
 * @param Callback 心跳回调闭包
 * @return 分配的句柄 ID
 */
uint64 FThrottledTickScheduler::RegisterIntervalTick(
    FOwnerValidator InValidator,
    float IntervalSeconds,
    FThrottledTickCallback Callback) {
    checkf(IsInGameThread(), TEXT("FThrottledTickScheduler::RegisterIntervalTick must be called on GameThread"));

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

/**
 * @brief 针对 UE5 UObject 弱引用的重载注册接口
 * @param Owner 宿主对象弱引用
 * @param IntervalSeconds 间隔秒数
 * @param Callback 心跳回调闭包
 * @return 分配的句柄 ID
 */
uint64 FThrottledTickScheduler::RegisterIntervalTick(
    TWeakObjectPtr<UObject> Owner,
    float IntervalSeconds,
    FThrottledTickCallback Callback) {
    checkf(IsInGameThread(), TEXT("FThrottledTickScheduler::RegisterIntervalTick must be called on GameThread"));

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

/**
 * @brief 依句柄 ID 注销心跳任务
 * @param HandleId 目标句柄 ID
 * @return 存在有效任务并成功标记注销返回 true
 */
bool FThrottledTickScheduler::UnregisterTick(uint64 HandleId) {
    checkf(IsInGameThread(), TEXT("FThrottledTickScheduler::UnregisterTick must be called on GameThread"));

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

/**
 * @brief 动态调整指定任务的间隔秒数
 * @param HandleId 目标句柄 ID
 * @param NewIntervalSeconds 新间隔秒数
 * @return 调整成功返回 true
 */
bool FThrottledTickScheduler::SetTickInterval(uint64 HandleId, float NewIntervalSeconds) {
    checkf(IsInGameThread(), TEXT("FThrottledTickScheduler::SetTickInterval must be called on GameThread"));

    if (HandleId == 0ULL) {
        return false;
    }

    const float NormalizedInterval = NewIntervalSeconds > 0.0f ? NewIntervalSeconds : 0.0f;
    for (FThrottledEntry& Entry : Entries) {
        if (Entry.HandleId == HandleId && !Entry.bPendingRemoval) {
            if (FMath::Abs(Entry.Interval - NormalizedInterval) <= KINDA_SMALL_NUMBER) {
                return true;
            }
            Entry.Interval = NormalizedInterval;
            if (NormalizedInterval <= 0.0f) {
                Entry.Elapsed = 0.0f;
            } else if (Entry.Elapsed >= NormalizedInterval) {
                Entry.Elapsed = NormalizedInterval;
            }
            return true;
        }
    }
    return false;
}

/**
 * @brief 查询目标任务是否处于活跃注册态
 * @param HandleId 目标句柄 ID
 * @return 活跃态返回 true
 */
bool FThrottledTickScheduler::IsTickActive(uint64 HandleId) const {
    checkf(IsInGameThread(), TEXT("FThrottledTickScheduler::IsTickActive must be called on GameThread"));

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

/**
 * @brief 查询当前已注册的心跳任务总数
 * @return 任务总数
 */
int32 FThrottledTickScheduler::GetRegisteredCount() const {
    checkf(IsInGameThread(), TEXT("FThrottledTickScheduler::GetRegisteredCount must be called on GameThread"));

    int32 ActiveCount = 0;
    for (const FThrottledEntry& Entry : Entries) {
        if (!Entry.bPendingRemoval) {
            ++ActiveCount;
        }
    }
    return ActiveCount;
}

/**
 * @brief 清空全部已注册心跳任务
 */
void FThrottledTickScheduler::ClearAll() {
    checkf(IsInGameThread(), TEXT("FThrottledTickScheduler::ClearAll must be called on GameThread"));

    if (bIsIterating) {
        for (FThrottledEntry& Entry : Entries) {
            Entry.bPendingRemoval = true;
        }
        return;
    }
    Entries.Empty();
}

/**
 * @brief 将预定义频率枚举转换为秒数间隔
 * @param Rate 预定义频率枚举
 * @return 对应秒数
 */
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
