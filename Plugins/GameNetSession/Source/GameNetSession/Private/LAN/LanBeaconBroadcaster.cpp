// Copyright Epic Games, Inc. All Rights Reserved.

#include "LAN/LanBeaconBroadcaster.h"

#include "Codec/NetPacketCodec.h"
#include "Logging/GameLogCategories.h"
#include "Misc/Guid.h"

/**
 * @brief 构造局域网信标广播器
 * @param InRoomExpirationSeconds 发现端房间无响应过期剔除阈值（秒）
 */
FLanBeaconBroadcaster::FLanBeaconBroadcaster(float InRoomExpirationSeconds) noexcept
    : DiscoveryTable(InRoomExpirationSeconds) {
}

/**
 * @brief 析构时自动停止并释放 UDP 通道
 */
FLanBeaconBroadcaster::~FLanBeaconBroadcaster() {
    Stop();
}

/**
 * @brief 以房主身份启动 UDP 监听信标，自动预编码房间负载并应答局域网探测请求
 * @param InHostedRoom 房间描述元数据
 * @param BindPort 绑定的本地 UDP 端口
 * @param BindIp 绑定的本地网卡地址
 * @return 通道打开且房间负载编码合法返回 true
 */
bool FLanBeaconBroadcaster::StartHosting(
    const FGameNetRoomDescriptor& InHostedRoom,
    int32 BindPort,
    const FString& BindIp
) {
    Stop();

    if (!ValidateHostedRoom(InHostedRoom)) {
        GAME_LOG(Warning, TEXT("[LAN/Beacon] 房主启动参数非法，拒绝开启广播信标。"));
        return false;
    }

    FGameNetRoomDescriptor CandidateRoom = InHostedRoom;
    CandidateRoom.bIsLanRoom = true;
    if (CandidateRoom.RoomId.IsEmpty()) {
        CandidateRoom.RoomId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    }

    TArray<uint8> CandidatePayload;
    if (!EncodeHostedRoomPayload(CandidateRoom, CandidatePayload)) {
        return false;
    }

    if (!SocketChannel.Open(BindIp, BindPort)) {
        return false;
    }

    HostedRoom = MoveTemp(CandidateRoom);
    CachedHostedRoomPayload = MoveTemp(CandidatePayload);
    Role = EGameNetLanRole::Hosting;
    GAME_LOG(
        Log,
        TEXT("[LAN/Beacon] 已开启局域网房主信标: Room=%s (%s), BeaconPort=%d"),
        *HostedRoom.RoomName,
        *HostedRoom.RoomId,
        SocketChannel.GetBoundPort()
    );
    return true;
}

/**
 * @brief 原子校验并动态刷新房主广播的房间元数据及预编码缓存（校验失败或超限时保留原有效状态）
 * @param UpdatedRoom 最新房间描述信息
 * @return 校验且编码合规并提交成功返回 true，否则保持原房间状态不变并返回 false
 */
bool FLanBeaconBroadcaster::UpdateHostedRoom(const FGameNetRoomDescriptor& UpdatedRoom) {
    if (!ValidateHostedRoom(UpdatedRoom)) {
        GAME_LOG(Warning, TEXT("[LAN/Beacon] 房主房间更新参数非法，已保留原有有效广播缓存。"));
        return false;
    }

    FGameNetRoomDescriptor CandidateRoom = UpdatedRoom;
    CandidateRoom.bIsLanRoom = true;
    if (CandidateRoom.RoomId.IsEmpty()) {
        CandidateRoom.RoomId = HostedRoom.RoomId.IsEmpty()
            ? FGuid::NewGuid().ToString(EGuidFormats::Digits)
            : HostedRoom.RoomId;
    }

    TArray<uint8> CandidatePayload;
    if (!EncodeHostedRoomPayload(CandidateRoom, CandidatePayload)) {
        return false;
    }

    HostedRoom = MoveTemp(CandidateRoom);
    CachedHostedRoomPayload = MoveTemp(CandidatePayload);
    return true;
}

/**
 * @brief 以客户端搜索者身份启动非阻塞 UDP 发现端口
 * @param BindPort 绑定的本地 UDP 端口
 * @param BindIp 绑定的本地网卡地址
 * @return 通道打开成功返回 true
 */
bool FLanBeaconBroadcaster::StartDiscovery(int32 BindPort, const FString& BindIp) {
    Stop();
    if (!SocketChannel.Open(BindIp, BindPort)) {
        return false;
    }

    Role = EGameNetLanRole::Discovering;
    GAME_LOG(Log, TEXT("[LAN/Beacon] 已开启局域网客户端发现端口: LocalPort=%d"), SocketChannel.GetBoundPort());
    return true;
}

/**
 * @brief 关闭 UDP 通道并重置发现表、房主缓存与工作角色
 */
void FLanBeaconBroadcaster::Stop() {
    SocketChannel.Close();
    DiscoveryTable.Reset();
    HostedRoom.Reset();
    CachedHostedRoomPayload.Reset();
    Role = EGameNetLanRole::Inactive;
}

/**
 * @brief 向局域网广播地址或指定单播地址发送房间探测报文
 * @param TargetEndpoint 目标 UDP 地址与端口
 * @return 数据报发送成功返回 true
 */
bool FLanBeaconBroadcaster::SendDiscoveryProbe(const FGameNetLanEndpoint& TargetEndpoint) {
    if (Role != EGameNetLanRole::Discovering || !SocketChannel.IsOpen()) {
        return false;
    }

    const uint32 ProbeSeqId = DiscoveryTable.AllocateAndRecordProbe(MonotonicClockSeconds);
    FNetPacketHeader Header;
    Header.MessageId = LAN_DISCOVERY_PROBE_MSG_ID;
    Header.SequenceId = ProbeSeqId;

    FNetPacketCodec::EncodeFrameTo(Header, {}, SendScratchBuffer);
    return SocketChannel.SendTo(SendScratchBuffer, TargetEndpoint.IpAddress, TargetEndpoint.Port);
}

/**
 * @brief 房主主动向目标地址推送一次房间宣告数据报（直接复用预编码 JSON 负载）
 * @param TargetEndpoint 目标 UDP 地址与端口
 * @return 数据报发送成功返回 true
 */
bool FLanBeaconBroadcaster::BroadcastAnnouncement(const FGameNetLanEndpoint& TargetEndpoint) {
    if (Role != EGameNetLanRole::Hosting || !SocketChannel.IsOpen()) {
        return false;
    }
    return SendAnnouncementFrame(0, TargetEndpoint.IpAddress, TargetEndpoint.Port);
}

/**
 * @brief 事件驱动等待底层 UDP 通道可读就绪
 * @param MaxWaitSeconds 最大等待秒数上限
 * @return 超时前检测到可读数据报到达返回 true
 */
bool FLanBeaconBroadcaster::WaitForIncomingData(double MaxWaitSeconds) const {
    if (Role == EGameNetLanRole::Inactive) {
        return false;
    }
    return SocketChannel.WaitReadable(MaxWaitSeconds);
}

/**
 * @brief 非阻塞轮询接收缓冲队列、应答探测请求并清理过期房间（重入安全）
 * @param DeltaTimeSeconds 距离上一帧的秒数增量
 */
void FLanBeaconBroadcaster::Tick(float DeltaTimeSeconds) {
    MonotonicClockSeconds += FMath::Max(0.0, static_cast<double>(DeltaTimeSeconds));
    if (!IsActive()) {
        return;
    }

    PumpIncomingDatagrams();

    if (Role == EGameNetLanRole::Discovering) {
        TArray<FString> ExpiredRoomIds;
        DiscoveryTable.PruneExpiredRooms(MonotonicClockSeconds, ExpiredRoomIds);
        for (const FString& ExpiredId : ExpiredRoomIds) {
            RoomExpiredDelegate.Broadcast(ExpiredId);
            if (!IsActive()) {
                break;
            }
        }
    }
}

/**
 * @brief 校验房主广播房间描述符的核心业务字段合法性
 * @param Room 待校验的房间描述符
 * @return 合法返回 true
 */
bool FLanBeaconBroadcaster::ValidateHostedRoom(const FGameNetRoomDescriptor& Room) noexcept {
    return !Room.RoomName.IsEmpty()
        && Room.MaxPlayers > 0
        && Room.CurrentPlayers >= 0
        && Room.CurrentPlayers <= Room.MaxPlayers
        && Room.HostGamePort > 0
        && Room.HostGamePort <= 65535;
}

/**
 * @brief 将指定房间描述符编码为紧凑 UTF-8 JSON 并校验不超过 MAX_BEACON_PAYLOAD_BYTES
 * @param Room 待编码的房间描述符
 * @param OutPayload 编码成功时写入的目标字节数组
 * @return 编码成功且长度合规返回 true
 */
bool FLanBeaconBroadcaster::EncodeHostedRoomPayload(
    const FGameNetRoomDescriptor& Room,
    TArray<uint8>& OutPayload
) const {
    const TGameStatusResult<void> SerializeResult = JsonSerializer.SerializeStructTo(Room, OutPayload);
    if (SerializeResult.IsFailure()) {
        OutPayload.Reset();
        GAME_LOG(Warning, TEXT("[LAN/Beacon] 房主房间元数据 JSON 编码失败: %s"), *SerializeResult.GetError().ToString());
        return false;
    }
    if (static_cast<uint32>(OutPayload.Num()) > MAX_BEACON_PAYLOAD_BYTES) {
        GAME_LOG(
            Warning,
            TEXT("[LAN/Beacon] 房主房间元数据超过单包上限: Bytes=%d, Max=%u"),
            OutPayload.Num(),
            MAX_BEACON_PAYLOAD_BYTES
        );
        OutPayload.Reset();
        return false;
    }
    return true;
}

/**
 * @brief 使用缓存的房主 JSON 负载组装宣告帧并发送至目标端点
 * @param SequenceId 报文头序列号
 * @param TargetIp 目标 IPv4 字符串
 * @param TargetPort 目标 UDP 端口
 * @return 发送成功返回 true
 */
bool FLanBeaconBroadcaster::SendAnnouncementFrame(uint32 SequenceId, const FString& TargetIp, int32 TargetPort) {
    if (CachedHostedRoomPayload.IsEmpty()) {
        return false;
    }

    FNetPacketHeader AnnounceHeader;
    AnnounceHeader.MessageId = LAN_ROOM_ANNOUNCE_MSG_ID;
    AnnounceHeader.SequenceId = SequenceId;

    FNetPacketCodec::EncodeFrameTo(AnnounceHeader, CachedHostedRoomPayload, SendScratchBuffer);
    return SocketChannel.SendTo(SendScratchBuffer, TargetIp, TargetPort);
}

/**
 * @brief 非阻塞排空当前 UDP 通道中的所有到达数据报并逐帧派发
 */
void FLanBeaconBroadcaster::PumpIncomingDatagrams() {
    for (int32 ProcessedCount = 0; ProcessedCount < MAX_PUMP_DATAGRAMS_PER_TICK && IsActive(); ++ProcessedCount) {
        if (!SocketChannel.TryReceiveDatagram(ReceivedDatagramScratch)) {
            break;
        }
        ProcessReceivedDatagram(ReceivedDatagramScratch);
    }
}

/**
 * @brief 校验并按当前角色派发单帧已接收的 UDP 数据报
 * @param Datagram 接收到的原始数据报与发送方地址
 */
void FLanBeaconBroadcaster::ProcessReceivedDatagram(const FLanReceivedDatagram& Datagram) {
    FNetPacketHeader DecodedHeader;
    if (!FNetPacketCodec::TryDecodeHeader(Datagram.Bytes, DecodedHeader)
        || !FNetPacketCodec::IsValidHeader(DecodedHeader, MAX_BEACON_PAYLOAD_BYTES)) {
        GAME_LOG_THROTTLE(1.0, Verbose, TEXT("[LAN/Beacon] 丢弃畸形 UDP 头部: Bytes=%d"), Datagram.Bytes.Num());
        return;
    }

    const uint32 ExpectedWireBytes =
        static_cast<uint32>(FNetPacketHeader::WIRE_HEADER_BYTES) + DecodedHeader.PayloadLength;
    if (static_cast<uint32>(Datagram.Bytes.Num()) != ExpectedWireBytes) {
        GAME_LOG_THROTTLE(
            1.0,
            Verbose,
            TEXT("[LAN/Beacon] 丢弃长度失配的 UDP 数据报: Actual=%d, Expected=%u"),
            Datagram.Bytes.Num(),
            ExpectedWireBytes
        );
        return;
    }

    if (Role == EGameNetLanRole::Hosting && DecodedHeader.MessageId == LAN_DISCOVERY_PROBE_MSG_ID) {
        (void)SendAnnouncementFrame(DecodedHeader.SequenceId, Datagram.SenderIp, Datagram.SenderPort);
        return;
    }

    if (Role == EGameNetLanRole::Discovering && DecodedHeader.MessageId == LAN_ROOM_ANNOUNCE_MSG_ID) {
        const TConstArrayView<uint8> PayloadView(
            Datagram.Bytes.GetData() + FNetPacketHeader::WIRE_HEADER_BYTES,
            static_cast<int32>(DecodedHeader.PayloadLength)
        );
        HandleAnnouncementPayload(DecodedHeader, PayloadView, Datagram.SenderIp);
    }
}

/**
 * @brief 客户端处理房主宣告负载并同步更新发现缓存表与事件委托
 * @param AnnounceHeader 宣告报文头
 * @param Payload UTF-8 JSON 负载切片
 * @param SenderIp 房主来源 IP
 */
void FLanBeaconBroadcaster::HandleAnnouncementPayload(
    const FNetPacketHeader& AnnounceHeader,
    TConstArrayView<uint8> Payload,
    const FString& SenderIp
) {
    DecodedRoomScratch.Reset();
    if (JsonSerializer.DeserializeStructTo(Payload, DecodedRoomScratch).IsFailure()) {
        return;
    }

    const TOptional<FGameNetRoomDescriptor> UpsertedRoom = DiscoveryTable.UpsertDiscoveredRoom(
        DecodedRoomScratch,
        SenderIp,
        AnnounceHeader.SequenceId,
        MonotonicClockSeconds
    );
    if (UpsertedRoom.IsSet()) {
        RoomDiscoveredDelegate.Broadcast(*UpsertedRoom);
    }
}
