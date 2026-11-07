/**
 * @file FGameDistId.cpp
 * @brief 64 位分布式单调递增雪花唯一 ID 原语与生成器实现
 */

#include "Types/FGameDistId.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"

FGameDistIdGenerator::FGameDistIdGenerator(uint16 InNodeId, int64 InCustomEpoch) noexcept
    : CustomEpoch(InCustomEpoch)
    , NodeId(InNodeId & FGameDistId::MAX_NODE_ID) {}

void FGameDistIdGenerator::Initialize(uint16 InNodeId, int64 InCustomEpoch) noexcept {
    FScopeLock Lock(&GeneratorLock);
    CustomEpoch = InCustomEpoch;
    NodeId = InNodeId & FGameDistId::MAX_NODE_ID;
    Sequence = 0;
    LastTimestampDelta = -1;
}

uint16 FGameDistIdGenerator::GetNodeId() const noexcept {
    FScopeLock Lock(&GeneratorLock);
    return NodeId;
}

int64 FGameDistIdGenerator::GetCustomEpoch() const noexcept {
    FScopeLock Lock(&GeneratorLock);
    return CustomEpoch;
}

FGameDistIdGenerator& FGameDistIdGenerator::Get() {
    static FGameDistIdGenerator Instance;
    return Instance;
}

int64 FGameDistIdGenerator::GetCurrentTimeMs() const {
    const FDateTime Now = FDateTime::UtcNow();
    return Now.ToUnixTimestamp() * 1000LL + Now.GetMillisecond();
}

int64 FGameDistIdGenerator::WaitUntilNextMillis(int64 InLastTimestamp) const {
    int64 Timestamp = GetCurrentTimeMs() - CustomEpoch;
    while (Timestamp <= InLastTimestamp) {
        FPlatformProcess::Sleep(0.0005f);
        Timestamp = GetCurrentTimeMs() - CustomEpoch;
    }
    return Timestamp;
}

FGameDistId FGameDistIdGenerator::NextId() {
    FScopeLock Lock(&GeneratorLock);

    int64 CurrentDelta = GetCurrentTimeMs() - CustomEpoch;

    if (CurrentDelta < LastTimestampDelta) {
        const int64 Drift = LastTimestampDelta - CurrentDelta;
        if (Drift <= 5) {
            FPlatformProcess::Sleep(static_cast<float>(Drift) * 0.001f);
            CurrentDelta = GetCurrentTimeMs() - CustomEpoch;
        }
        if (CurrentDelta < LastTimestampDelta) {
            CurrentDelta = LastTimestampDelta;
        }
    }

    if (CurrentDelta == LastTimestampDelta) {
        Sequence = (Sequence + 1U) & FGameDistId::MAX_SEQUENCE;
        if (Sequence == 0) {
            CurrentDelta = WaitUntilNextMillis(LastTimestampDelta);
        }
    } else {
        Sequence = 0;
    }

    LastTimestampDelta = CurrentDelta;
    return FGameDistId::FromParts(CurrentDelta, NodeId, Sequence);
}
