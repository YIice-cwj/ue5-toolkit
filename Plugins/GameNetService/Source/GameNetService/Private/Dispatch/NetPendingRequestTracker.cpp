// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dispatch/NetPendingRequestTracker.h"

/**
 * @brief 分配下一个非零递增请求序列号（自动跳过保留给主动推送的 0 值）
 * @return 非零请求序列号
 */
uint32 FNetPendingRequestTracker::AllocateSequenceId() noexcept {
    ++NextSequenceCounter;
    if (NextSequenceCounter == 0) {
        NextSequenceCounter = 1;
    }
    return NextSequenceCounter;
}

/**
 * @brief 登记一条等待对端回包的异步请求及超时回调
 * @param SequenceId 发包使用的非零序列号
 * @param ExpectedResponseMessageId 期望匹配的响应协议 ID
 * @param TimeoutSeconds 超时秒数上限
 * @param Callback 完成、断线或超时回调闭包
 */
void FNetPendingRequestTracker::RegisterPendingRequest(
    uint32 SequenceId,
    uint32 ExpectedResponseMessageId,
    float TimeoutSeconds,
    FNetPendingResponseHandler Callback
) {
    if (SequenceId == 0 || !Callback) {
        return;
    }

    FPendingRequestEntry Entry;
    Entry.SequenceId = SequenceId;
    Entry.ExpectedResponseMessageId = ExpectedResponseMessageId;
    Entry.RemainingSeconds = FMath::Max(TimeoutSeconds, MIN_TIMEOUT_FLOOR_SECONDS);
    Entry.Callback = MoveTemp(Callback);
    PendingRequests.Add(SequenceId, MoveTemp(Entry));
}

/**
 * @brief 尝试用到达的下行数据包匹配并结算对应的挂起异步请求
 * @param Header 到达报文头
 * @param Payload 到达负载二进制切片
 * @return 命中并结算一条挂起请求时返回 true
 */
bool FNetPendingRequestTracker::TryCompleteResponse(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload) {
    if (Header.SequenceId == 0) {
        return false;
    }

    const FPendingRequestEntry* Existing = PendingRequests.Find(Header.SequenceId);
    if (Existing == nullptr) {
        return false;
    }

    const bool bMessageMatches =
        Existing->ExpectedResponseMessageId == 0 || Existing->ExpectedResponseMessageId == Header.MessageId;
    if (!bMessageMatches) {
        return false;
    }

    FPendingRequestEntry CompletedEntry;
    PendingRequests.RemoveAndCopyValue(Header.SequenceId, CompletedEntry);
    CompletedEntry.Callback(ENetRequestResult::Success, Header, Payload);
    return true;
}

/**
 * @brief 推进所有挂起异步请求的超时计时器，到期请求立即触发 TimedOut 回调并清理
 * @param DeltaTimeSeconds 帧间隔秒数
 */
void FNetPendingRequestTracker::TickTimeouts(float DeltaTimeSeconds) {
    if (PendingRequests.IsEmpty() || DeltaTimeSeconds <= 0.0f) {
        return;
    }

    TArray<uint32> ExpiredSequenceIds;
    for (TPair<uint32, FPendingRequestEntry>& Pair : PendingRequests) {
        Pair.Value.RemainingSeconds -= DeltaTimeSeconds;
        if (Pair.Value.RemainingSeconds <= 0.0f) {
            ExpiredSequenceIds.Add(Pair.Key);
        }
    }

    const FNetPacketHeader EmptyHeader;
    for (uint32 SequenceId : ExpiredSequenceIds) {
        FPendingRequestEntry ExpiredEntry;
        if (PendingRequests.RemoveAndCopyValue(SequenceId, ExpiredEntry)) {
            ExpiredEntry.Callback(ENetRequestResult::TimedOut, EmptyHeader, {});
        }
    }
}

/**
 * @brief 立即以指定失败原因结算并清空全部挂起请求
 * @param FailureReason 失败原因枚举
 */
void FNetPendingRequestTracker::FailAll(ENetRequestResult FailureReason) {
    if (PendingRequests.IsEmpty()) {
        return;
    }

    TMap<uint32, FPendingRequestEntry> DrainedRequests = MoveTemp(PendingRequests);
    PendingRequests.Reset();

    const FNetPacketHeader EmptyHeader;
    for (TPair<uint32, FPendingRequestEntry>& Pair : DrainedRequests) {
        Pair.Value.Callback(FailureReason, EmptyHeader, {});
    }
}

