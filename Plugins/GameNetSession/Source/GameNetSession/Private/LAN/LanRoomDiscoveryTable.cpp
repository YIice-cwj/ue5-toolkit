// Copyright Epic Games, Inc. All Rights Reserved.

#include "LAN/LanRoomDiscoveryTable.h"

#include "Logging/GameLogCategories.h"

/**
 * @brief 构造房间发现缓存表
 * @param InExpirationSeconds 房间无宣告刷新过期剔除秒数阈值
 */
FLanRoomDiscoveryTable::FLanRoomDiscoveryTable(float InExpirationSeconds) noexcept
    : ExpirationSeconds(FMath::Max(MIN_EXPIRATION_FLOOR_SECONDS, InExpirationSeconds)) {
}

/**
 * @brief 分配下一个非零局域网探测序列号并记录发出时间戳（自动维持有界探测表容量）
 * @param SentClockSeconds 探测发出时刻秒数
 * @return 分配的非零探测序列号
 */
uint32 FLanRoomDiscoveryTable::AllocateAndRecordProbe(double SentClockSeconds) {
    if (PendingProbeSendTimes.Num() >= MAX_PENDING_PROBES) {
        uint32 OldestSeq = 0;
        double OldestTime = TNumericLimits<double>::Max();
        for (const TPair<uint32, double>& Pair : PendingProbeSendTimes) {
            if (Pair.Value < OldestTime) {
                OldestTime = Pair.Value;
                OldestSeq = Pair.Key;
            }
        }
        if (OldestSeq != 0) {
            PendingProbeSendTimes.Remove(OldestSeq);
        }
    }

    ++NextProbeSequenceId;
    if (NextProbeSequenceId == 0) {
        NextProbeSequenceId = 1;
    }
    PendingProbeSendTimes.Add(NextProbeSequenceId, SentClockSeconds);
    return NextProbeSequenceId;
}

/**
 * @brief 校验、防注入清洗来源地址、测算多房主广播探测 RTT 并原地插入或更新发现房间记录
 * @param IncomingRoom 解码出的房间描述符
 * @param SenderIp 数据报发送方 IPv4 字符串
 * @param EchoedSequenceId 宣告报文头回显的探测序列号
 * @param CurrentClockSeconds 当前单调时钟秒数
 * @return 房间字段合法且未超容量上限时返回归一化后的房间描述符，否则返回空值
 */
TOptional<FGameNetRoomDescriptor> FLanRoomDiscoveryTable::UpsertDiscoveredRoom(
    FGameNetRoomDescriptor IncomingRoom,
    const FString& SenderIp,
    uint32 EchoedSequenceId,
    double CurrentClockSeconds
) {
    if (IncomingRoom.RoomId.IsEmpty()
        || IncomingRoom.RoomName.IsEmpty()
        || IncomingRoom.MaxPlayers <= 0
        || IncomingRoom.CurrentPlayers < 0
        || IncomingRoom.CurrentPlayers > IncomingRoom.MaxPlayers
        || IncomingRoom.HostGamePort <= 0
        || IncomingRoom.HostGamePort > 65535) {
        return NullOpt;
    }

    if (IncomingRoom.HostAddress.IsEmpty()) {
        IncomingRoom.HostAddress = SenderIp;
    }
    if (!IsSafeHostAddress(IncomingRoom.HostAddress)) {
        GAME_LOG_THROTTLE(
            1.0,
            Warning,
            TEXT("[LAN/Table] 拦截含非法字符或为空的房主地址宣告: RoomId=%s"),
            *IncomingRoom.RoomId
        );
        return NullOpt;
    }

    IncomingRoom.bIsLanRoom = true;

    FDiscoveredRoomRecord* ExistingRecord = DiscoveredRooms.Find(IncomingRoom.RoomId);
    if (ExistingRecord == nullptr && DiscoveredRooms.Num() >= MAX_DISCOVERED_ROOMS) {
        GAME_LOG_THROTTLE(
            1.0,
            Warning,
            TEXT("[LAN/Table] 局域网房间发现缓存已达上限 (%d)，丢弃新房间宣告: RoomId=%s"),
            MAX_DISCOVERED_ROOMS,
            *IncomingRoom.RoomId
        );
        return NullOpt;
    }

    if (const double* ProbeSentTime = (EchoedSequenceId != 0) ? PendingProbeSendTimes.Find(EchoedSequenceId) : nullptr) {
        const double ElapsedMs = FMath::Max(0.0, (CurrentClockSeconds - *ProbeSentTime) * 1000.0);
        IncomingRoom.PingMilliseconds = FMath::RoundToInt32(ElapsedMs);
    } else if (ExistingRecord != nullptr) {
        if (IncomingRoom.PingMilliseconds <= 0 && ExistingRecord->Descriptor.PingMilliseconds > 0) {
            IncomingRoom.PingMilliseconds = ExistingRecord->Descriptor.PingMilliseconds;
        }
    }

    if (ExistingRecord != nullptr) {
        ExistingRecord->Descriptor = MoveTemp(IncomingRoom);
        ExistingRecord->LastSeenTimeSeconds = CurrentClockSeconds;
        return ExistingRecord->Descriptor;
    }

    FDiscoveredRoomRecord& NewRecord = DiscoveredRooms.Add(IncomingRoom.RoomId);
    NewRecord.Descriptor = MoveTemp(IncomingRoom);
    NewRecord.LastSeenTimeSeconds = CurrentClockSeconds;
    return NewRecord.Descriptor;
}

/**
 * @brief 扫描并移除所有超过过期阈值的房间记录与过期探测时间戳并就地写入外部复用数组
 * @param CurrentClockSeconds 当前单调时钟秒数
 * @param OutExpiredIds 本次被剔除的所有过期 RoomId 目标数组
 */
void FLanRoomDiscoveryTable::PruneExpiredRooms(double CurrentClockSeconds, TArray<FString>& OutExpiredIds) {
    OutExpiredIds.Reset();
    for (auto It = DiscoveredRooms.CreateIterator(); It; ++It) {
        if ((CurrentClockSeconds - It.Value().LastSeenTimeSeconds) > static_cast<double>(ExpirationSeconds)) {
            OutExpiredIds.Add(It.Key());
            It.RemoveCurrent();
        }
    }

    for (auto It = PendingProbeSendTimes.CreateIterator(); It; ++It) {
        if ((CurrentClockSeconds - It.Value()) > static_cast<double>(ExpirationSeconds)) {
            It.RemoveCurrent();
        }
    }
}

/**
 * @brief 获取当前未过期局域网房间的描述符快照并就地写入外部复用数组
 * @param OutSnapshot 目标接收数组
 */
void FLanRoomDiscoveryTable::GetRoomsSnapshot(TArray<FGameNetRoomDescriptor>& OutSnapshot) const {
    OutSnapshot.Reset();
    OutSnapshot.Reserve(DiscoveredRooms.Num());
    for (const TPair<FString, FDiscoveredRoomRecord>& Pair : DiscoveredRooms) {
        OutSnapshot.Add(Pair.Value.Descriptor);
    }
}

/**
 * @brief 清空所有已发现房间缓存与挂起探测时间记录
 */
void FLanRoomDiscoveryTable::Reset() {
    DiscoveredRooms.Empty();
    PendingProbeSendTimes.Empty();
    NextProbeSequenceId = 0;
}

/**
 * @brief 校验房主主机地址字段不含空白、控制符或 Travel URL 选项注入字符
 * @param HostToken 待校验的主机地址字符串
 * @return 合法返回 true
 */
bool FLanRoomDiscoveryTable::IsSafeHostAddress(const FString& HostToken) noexcept {
    if (HostToken.IsEmpty()) {
        return false;
    }

    for (const TCHAR Ch : HostToken) {
        if (FChar::IsWhitespace(Ch) || FChar::IsControl(Ch) || Ch == TEXT('?') || Ch == TEXT('#') || Ch == TEXT('/') || Ch == TEXT('\\')) {
            return false;
        }
    }
    return true;
}
