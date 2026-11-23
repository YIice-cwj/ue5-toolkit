// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dispatch/NetPendingRequestTracker.h"

/**
 * @brief 分配下一个非零递增请求序列号（自动跳过保留给主动推送的 0 值与仍在途的冲突序号）
 * @return 非零请求序列号
 */
uint32 FNetPendingRequestTracker::AllocateSequenceId() noexcept {
    do {
        ++NextSequenceCounter;
        if (NextSequenceCounter == 0) {
            NextSequenceCounter = 1;
        }
    } while (PendingRequests.Contains(NextSequenceCounter));
    return NextSequenceCounter;
}

/**
 * @brief 登记一条等待对端回包的异步请求及超时回调
 * @param SequenceId 发包使用的非零序列号
 * @param ExpectedResponseMessageId 期望匹配的响应协议 ID
 * @param TimeoutSeconds 超时秒数上限
 * @param Callback 完成、取消、断线或超时回调闭包
 * @param CancelRegistration 可选关联的取消令牌注册句柄
 */
void FNetPendingRequestTracker::RegisterPendingRequest(
    uint32 SequenceId,
    uint32 ExpectedResponseMessageId,
    float TimeoutSeconds,
    FNetPendingResponseHandler Callback,
    FCancellationTokenRegistration CancelRegistration
) {
    if (SequenceId == 0 || !Callback) {
        return;
    }

    FNetPendingResponseHandler ReplacedCallback;
    const FSetElementId ExistingId = PendingRequests.FindId(SequenceId);
    if (ExistingId.IsValidId()) {
        ReplacedCallback = MoveTemp(PendingRequests.Get(ExistingId).Value.Callback);
        PendingRequests.Remove(ExistingId);
    }

    FPendingRequestEntry Entry;
    Entry.ExpectedResponseMessageId = ExpectedResponseMessageId;
    Entry.RemainingSeconds = FMath::Max(TimeoutSeconds, MIN_TIMEOUT_FLOOR_SECONDS);
    Entry.Callback = MoveTemp(Callback);
    Entry.CancelRegistration = MoveTemp(CancelRegistration);
    PendingRequests.Add(SequenceId, MoveTemp(Entry));

    if (ReplacedCallback) {
        const FNetPacketHeader EmptyHeader;
        ReplacedCallback(ENetRequestResult::Cancelled, EmptyHeader, {});
    }
}

/**
 * @brief 为已登记的在途请求回绑取消令牌注册句柄
 * @param SequenceId 在途请求序列号
 * @param CancelRegistration 取消令牌注销控制句柄
 * @return 请求仍在途并成功挂载返回 true；若已被同步取消或结算返回 false
 */
bool FNetPendingRequestTracker::AttachCancellationRegistration(
    uint32 SequenceId,
    FCancellationTokenRegistration CancelRegistration
) {
    FPendingRequestEntry* Entry = PendingRequests.Find(SequenceId);
    if (Entry == nullptr) {
        return false;
    }

    Entry->CancelRegistration = MoveTemp(CancelRegistration);
    return true;
}

/**
 * @brief 尝试用到达的下行数据包匹配并结算对应的挂起异步请求（单次哈希定位）
 * @param Header 到达报文头
 * @param Payload 到达负载二进制切片
 * @return 命中并结算一条挂起请求时返回 true
 */
bool FNetPendingRequestTracker::TryCompleteResponse(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload) {
    if (Header.SequenceId == 0) {
        return false;
    }

    const FSetElementId ElementId = PendingRequests.FindId(Header.SequenceId);
    if (!ElementId.IsValidId()) {
        return false;
    }

    FPendingRequestEntry& Existing = PendingRequests.Get(ElementId).Value;
    const bool bMessageMatches =
        Existing.ExpectedResponseMessageId == 0 || Existing.ExpectedResponseMessageId == Header.MessageId;
    if (!bMessageMatches) {
        return false;
    }

    FPendingRequestEntry CompletedEntry = MoveTemp(Existing);
    PendingRequests.Remove(ElementId);
    CompletedEntry.CancelRegistration.Unregister();
    CompletedEntry.Callback(ENetRequestResult::Success, Header, Payload);
    return true;
}

/**
 * @brief 主动取消并移除指定序列号的挂起异步请求
 * @param SequenceId 待取消的请求序列号
 * @return 成功找到并取消返回 true，不存在或已结算返回 false
 */
bool FNetPendingRequestTracker::CancelRequest(uint32 SequenceId) {
    if (SequenceId == 0) {
        return false;
    }

    const FSetElementId ElementId = PendingRequests.FindId(SequenceId);
    if (!ElementId.IsValidId()) {
        return false;
    }

    FPendingRequestEntry CancelledEntry = MoveTemp(PendingRequests.Get(ElementId).Value);
    PendingRequests.Remove(ElementId);
    CancelledEntry.CancelRegistration.Unregister();

    const FNetPacketHeader EmptyHeader;
    CancelledEntry.Callback(ENetRequestResult::Cancelled, EmptyHeader, {});
    return true;
}

/**
 * @brief 推进所有挂起异步请求的超时计时器，到期请求立即触发 TimedOut 回调并清理（重入安全）
 * @param DeltaTimeSeconds 帧间隔秒数
 */
void FNetPendingRequestTracker::TickTimeouts(float DeltaTimeSeconds) {
    if (PendingRequests.IsEmpty() || DeltaTimeSeconds <= 0.0f) {
        return;
    }

    TArray<FPendingRequestEntry, TInlineAllocator<8>> ExpiredEntries;
    for (auto It = PendingRequests.CreateIterator(); It; ++It) {
        It.Value().RemainingSeconds -= DeltaTimeSeconds;
        if (It.Value().RemainingSeconds <= 0.0f) {
            ExpiredEntries.Add(MoveTemp(It.Value()));
            It.RemoveCurrent();
        }
    }

    const FNetPacketHeader EmptyHeader;
    for (FPendingRequestEntry& ExpiredEntry : ExpiredEntries) {
        ExpiredEntry.CancelRegistration.Unregister();
        ExpiredEntry.Callback(ENetRequestResult::TimedOut, EmptyHeader, {});
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
        Pair.Value.CancelRegistration.Unregister();
        Pair.Value.Callback(FailureReason, EmptyHeader, {});
    }
}

/**
 * @brief 重置请求序列号计数器与全部缓存
 */
void FNetPendingRequestTracker::Reset() noexcept {
    NextSequenceCounter = 0;
    PendingRequests.Reset();
}
