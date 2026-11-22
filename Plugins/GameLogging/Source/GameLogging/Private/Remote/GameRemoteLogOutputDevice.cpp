// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameRemoteLogOutputDevice.cpp
 * @brief 线程安全远程日志捕获输出设备实现（TLockFreeRingBuffer + 原子容量预留 + L1 内联黑名单）
 */

#include "Remote/GameRemoteLogOutputDevice.h"

#include "HAL/PlatformTime.h"
#include "Misc/ScopeRWLock.h"
#include "Remote/FRemoteLogReentrancyScope.h"

namespace {
constexpr double UnspecifiedTimestampSentinel = -1.0;
} // namespace

FGameRemoteLogOutputDevice::FGameRemoteLogOutputDevice()
    : RingBuffer(MakeUnique<FRingBuffer>()) {
    IgnoredCategories.Add(FName(TEXT("LogGameMessage")));
    IgnoredCategories.Add(FName(TEXT("LogGameNetService")));
    IgnoredCategories.Add(FName(TEXT("LogGameNetCore")));
}

FGameRemoteLogOutputDevice::~FGameRemoteLogOutputDevice() {
    Clear();
}

bool FGameRemoteLogOutputDevice::CanBeUsedOnMultipleThreads() const {
    return true;
}

void FGameRemoteLogOutputDevice::Serialize(
    const TCHAR* V,
    ELogVerbosity::Type Verbosity,
    const class FName& Category,
    const double Time
) {
    EnqueueLogInternal(V, Verbosity, Category, Time);
}

void FGameRemoteLogOutputDevice::Serialize(
    const TCHAR* V,
    ELogVerbosity::Type Verbosity,
    const class FName& Category
) {
    EnqueueLogInternal(V, Verbosity, Category, UnspecifiedTimestampSentinel);
}

int32 FGameRemoteLogOutputDevice::DrainBatch(TArray<FGameRemoteLogEntry>& OutBatch, int32 MaxCount) {
    if (MaxCount <= 0) {
        return 0;
    }

    const int32 CurrentQueued = QueuedCount.load(std::memory_order_relaxed);
    const int32 ExpectedCount = FMath::Min(MaxCount, FMath::Max(0, CurrentQueued));
    if (ExpectedCount > 0) {
        OutBatch.Reserve(OutBatch.Num() + ExpectedCount);
    }

    int32 Drained = 0;
    FGameRemoteLogEntry Item;
    while (Drained < MaxCount && RingBuffer->Dequeue(Item)) {
        OutBatch.Add(MoveTemp(Item));
        ++Drained;
    }

    if (Drained > 0) {
        QueuedCount.fetch_sub(Drained, std::memory_order_release);
    }

    return Drained;
}

int32 FGameRemoteLogOutputDevice::DiscardBatch(int32 MaxCount) {
    if (MaxCount <= 0) {
        return 0;
    }

    int32 Discarded = 0;
    FGameRemoteLogEntry Dump;
    while (Discarded < MaxCount && RingBuffer->Dequeue(Dump)) {
        ++Discarded;
    }

    if (Discarded > 0) {
        QueuedCount.fetch_sub(Discarded, std::memory_order_release);
    }

    return Discarded;
}

void FGameRemoteLogOutputDevice::SetMinVerbosity(ELogVerbosity::Type InMinVerbosity) noexcept {
    MinVerbosity.store(
        static_cast<uint8>(InMinVerbosity & ELogVerbosity::VerbosityMask),
        std::memory_order_relaxed
    );
}

ELogVerbosity::Type FGameRemoteLogOutputDevice::GetMinVerbosity() const noexcept {
    return static_cast<ELogVerbosity::Type>(MinVerbosity.load(std::memory_order_relaxed));
}

void FGameRemoteLogOutputDevice::SetMaxCapacity(int32 InCapacity) noexcept {
    if (InCapacity > 0) {
        MaxCapacity.store(FMath::Min(InCapacity, MAX_CAPACITY), std::memory_order_relaxed);
    }
}

int32 FGameRemoteLogOutputDevice::GetMaxCapacity() const noexcept {
    return MaxCapacity.load(std::memory_order_relaxed);
}

bool FGameRemoteLogOutputDevice::IsEmpty() const noexcept {
    return RingBuffer->IsEmpty();
}

int32 FGameRemoteLogOutputDevice::GetQueuedCount() const noexcept {
    return FMath::Max(0, QueuedCount.load(std::memory_order_relaxed));
}

int64 FGameRemoteLogOutputDevice::GetDroppedCount() const noexcept {
    return DroppedCount.load(std::memory_order_relaxed);
}

void FGameRemoteLogOutputDevice::AddIgnoredCategory(FName CategoryName) {
    if (CategoryName.IsNone()) {
        return;
    }
    FWriteScopeLock WriteLock(IgnoredCategoriesLock);
    IgnoredCategories.AddUnique(CategoryName);
}

void FGameRemoteLogOutputDevice::RemoveIgnoredCategory(FName CategoryName) {
    if (CategoryName.IsNone()) {
        return;
    }
    FWriteScopeLock WriteLock(IgnoredCategoriesLock);
    IgnoredCategories.RemoveSingleSwap(CategoryName);
}

bool FGameRemoteLogOutputDevice::IsCategoryIgnored(FName CategoryName) const {
    if (CategoryName.IsNone()) {
        return false;
    }
    FReadScopeLock ReadLock(IgnoredCategoriesLock);
    return IgnoredCategories.Contains(CategoryName);
}

void FGameRemoteLogOutputDevice::Clear() {
    (void)DiscardBatch(MAX_CAPACITY);
    DroppedCount.store(0, std::memory_order_relaxed);
}

void FGameRemoteLogOutputDevice::EnqueueLogInternal(
    const TCHAR* V,
    ELogVerbosity::Type Verbosity,
    const class FName& Category,
    double Time
) {
    const uint8 PureVerbosity = static_cast<uint8>(Verbosity & ELogVerbosity::VerbosityMask);
    if (PureVerbosity == ELogVerbosity::NoLogging || PureVerbosity > MinVerbosity.load(std::memory_order_relaxed)) {
        return;
    }

    if (Game::Log::Private::FRemoteLogReentrancyScope::IsHandling()) {
        return;
    }
    const Game::Log::Private::FRemoteLogReentrancyScope ReentrancyGuard;

    if (IsCategoryIgnored(Category)) {
        return;
    }

    if (!TryReserveCapacitySlot()) {
        DroppedCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const double ResolvedTime = (Time >= 0.0) ? Time : FPlatformTime::Seconds();
    const int64 Frame = static_cast<int64>(GFrameCounter);
    if (!RingBuffer->Emplace(Category, PureVerbosity, FString(V != nullptr ? V : TEXT("")), ResolvedTime, Frame)) {
        QueuedCount.fetch_sub(1, std::memory_order_release);
        DroppedCount.fetch_add(1, std::memory_order_relaxed);
    }
}

bool FGameRemoteLogOutputDevice::TryReserveCapacitySlot() noexcept {
    int32 CurrentQueued = QueuedCount.load(std::memory_order_relaxed);
    const int32 CapacityLimit = MaxCapacity.load(std::memory_order_relaxed);
    while (CurrentQueued < CapacityLimit) {
        if (QueuedCount.compare_exchange_weak(
                CurrentQueued,
                CurrentQueued + 1,
                std::memory_order_acquire,
                std::memory_order_relaxed
            )) {
            return true;
        }
    }
    return false;
}
