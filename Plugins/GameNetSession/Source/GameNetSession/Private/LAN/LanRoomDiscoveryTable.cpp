// Copyright Epic Games, Inc. All Rights Reserved.

#include "LAN/LanRoomDiscoveryTable.h"

/**
 * @brief 构造房间发现缓存表
 * @param InExpirationSeconds 房间无宣告刷新过期剔除秒数阈值
 */
FLanRoomDiscoveryTable::FLanRoomDiscoveryTable(float InExpirationSeconds) noexcept
    : ExpirationSeconds(FMath::Max(MIN_EXPIRATION_FLOOR_SECONDS, InExpirationSeconds)) {
}

/**
 * @brief 分配下一个非零局域网探测序列号并记录发出时间戳
 * @param SentClockSeconds 探测发出时刻秒数
 * @return 分配的非零探测序列号
 */
uint32 FLanRoomDiscoveryTable::AllocateAndRecordProbe(double SentClockSeconds) {
    ++NextProbeSequenceId;
    if (NextProbeSequenceId == 0) {
        NextProbeSequenceId = 1;
    }
    PendingProbeSendTimes.Add(NextProbeSequenceId, SentClockSeconds);
    return NextProbeSequenceId;
}

/**
 * @brief 校验、补全来源地址、计算探测回显 RTT 并原地插入或更新发现房间记录
 * @param IncomingRoom 解码出的房间描述符
 * @param SenderIp 数据报发送方 IPv4 字符串
 * @param EchoedSequenceId 宣告报文头回显的探测序列号
 * @param CurrentClockSeconds 当前单调时钟秒数
 * @return 房间字段合法时返回归一化后的房间描述符，字段非法时返回空值
 */
TOptional<FGameNetRoomDescriptor> FLanRoomDiscoveryTable::UpsertDiscoveredRoom(
    FGameNetRoomDescriptor IncomingRoom,
    const FString& SenderIp,
    uint32 EchoedSequenceId,
    double CurrentClockSeconds
) {
    if (IncomingRoom.RoomId.IsEmpty() || IncomingRoom.RoomName.IsEmpty() || IncomingRoom.MaxPlayers <= 0) {
        return NullOpt;
    }

    if (IncomingRoom.HostAddress.IsEmpty()) {
        IncomingRoom.HostAddress = SenderIp;
    }

    double ProbeSentTime = 0.0;
    if (EchoedSequenceId != 0 && PendingProbeSendTimes.RemoveAndCopyValue(EchoedSequenceId, ProbeSentTime)) {
        const double ElapsedMs = FMath::Max(0.0, (CurrentClockSeconds - ProbeSentTime) * 1000.0);
        IncomingRoom.PingMilliseconds = FMath::RoundToInt32(ElapsedMs);
    } else if (const FDiscoveredRoomRecord* ExistingRecord = DiscoveredRooms.Find(IncomingRoom.RoomId)) {
        if (IncomingRoom.PingMilliseconds <= 0 && ExistingRecord->Descriptor.PingMilliseconds > 0) {
            IncomingRoom.PingMilliseconds = ExistingRecord->Descriptor.PingMilliseconds;
        }
    }

    FDiscoveredRoomRecord& Record = DiscoveredRooms.FindOrAdd(IncomingRoom.RoomId);
    Record.Descriptor = MoveTemp(IncomingRoom);
    Record.LastSeenTimeSeconds = CurrentClockSeconds;
    return Record.Descriptor;
}

/**
 * @brief 扫描并移除所有超过过期阈值的房间记录与失联悬挂探测时间戳
 * @param CurrentClockSeconds 当前单调时钟秒数
 * @return 本次被剔除的所有过期 RoomId 列表
 */
TArray<FString> FLanRoomDiscoveryTable::PruneExpiredRooms(double CurrentClockSeconds) {
    TArray<FString> ExpiredIds;
    PruneExpiredRoomsTo(CurrentClockSeconds, ExpiredIds);
    return ExpiredIds;
}

/**
 * @brief 扫描并移除所有超过过期阈值的房间记录与失联悬挂探测时间戳并就地写入外部复用数组（单次迭代安全剔除，零额外堆分配）
 * @param CurrentClockSeconds 当前单调时钟秒数
 * @param OutExpiredIds 本次被剔除的所有过期 RoomId 目标数组
 */
void FLanRoomDiscoveryTable::PruneExpiredRoomsTo(double CurrentClockSeconds, TArray<FString>& OutExpiredIds) {
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
 * @brief 获取当前所有未过期局域网房间的描述符快照列表
 * @return 房间描述符数组
 */
TArray<FGameNetRoomDescriptor> FLanRoomDiscoveryTable::GetRoomsSnapshot() const {
    TArray<FGameNetRoomDescriptor> Snapshot;
    GetRoomsSnapshotTo(Snapshot);
    return Snapshot;
}

/**
 * @brief 获取当前未过期局域网房间的描述符快照并就地写入外部复用数组
 * @param OutSnapshot 目标接收数组
 */
void FLanRoomDiscoveryTable::GetRoomsSnapshotTo(TArray<FGameNetRoomDescriptor>& OutSnapshot) const {
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

