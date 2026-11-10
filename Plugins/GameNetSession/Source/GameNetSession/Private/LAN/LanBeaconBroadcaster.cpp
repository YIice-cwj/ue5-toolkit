// Copyright Epic Games, Inc. All Rights Reserved.

#include "LAN/LanBeaconBroadcaster.h"

#include "Codec/NetPacketCodec.h"
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
 * @brief 以房主身份启动 UDP 监听信标，自动应答局域网探测请求
 * @param InHostedRoom 房间描述元数据
 * @param BindPort 绑定的本地 UDP 端口
 * @param BindIp 绑定的本地网卡地址
 * @return 通道打开成功返回 true
 */
bool FLanBeaconBroadcaster::StartHosting(
    const FGameNetRoomDescriptor& InHostedRoom,
    int32 BindPort,
    const FString& BindIp
) {
    Stop();

    if (InHostedRoom.RoomName.IsEmpty() || InHostedRoom.MaxPlayers <= 0 || InHostedRoom.HostGamePort <= 0) {
        return false;
    }
    if (!SocketChannel.Open(BindIp, BindPort)) {
        return false;
    }

    HostedRoom = InHostedRoom;
    HostedRoom.bIsLanRoom = true;
    if (HostedRoom.RoomId.IsEmpty()) {
        HostedRoom.RoomId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    }

    Role = EGameNetLanRole::Hosting;
    return true;
}

/**
 * @brief 动态刷新房主广播的房间人数或地图等描述信息
 * @param UpdatedRoom 最新房间描述信息
 */
void FLanBeaconBroadcaster::UpdateHostedRoom(const FGameNetRoomDescriptor& UpdatedRoom) {
    const FString PreviousRoomId = HostedRoom.RoomId;
    HostedRoom = UpdatedRoom;
    HostedRoom.bIsLanRoom = true;
    if (HostedRoom.RoomId.IsEmpty()) {
        HostedRoom.RoomId = PreviousRoomId.IsEmpty()
            ? FGuid::NewGuid().ToString(EGuidFormats::Digits)
            : PreviousRoomId;
    }
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
    return true;
}

/**
 * @brief 关闭 UDP 通道并重置发现表与工作角色
 */
void FLanBeaconBroadcaster::Stop() {
    SocketChannel.Close();
    DiscoveryTable.Reset();
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

    const TArray<uint8> WireFrame = FNetPacketCodec::EncodeFrame(Header, {});
    return SocketChannel.SendTo(WireFrame, TargetEndpoint.IpAddress, TargetEndpoint.Port);
}

/**
 * @brief 房主主动向目标地址推送一次房间宣告数据报
 * @param TargetEndpoint 目标 UDP 地址与端口
 * @return 数据报发送成功返回 true
 */
bool FLanBeaconBroadcaster::BroadcastAnnouncement(const FGameNetLanEndpoint& TargetEndpoint) {
    if (Role != EGameNetLanRole::Hosting || !SocketChannel.IsOpen()) {
        return false;
    }

    const TGameStatusResult<TArray<uint8>> EncodedRoom = JsonSerializer.SerializeStruct(HostedRoom);
    if (EncodedRoom.IsFailure() || static_cast<uint32>(EncodedRoom.GetValue().Num()) > MAX_BEACON_PAYLOAD_BYTES) {
        return false;
    }

    FNetPacketHeader Header;
    Header.MessageId = LAN_ROOM_ANNOUNCE_MSG_ID;
    Header.SequenceId = 0;

    const TArray<uint8> WireFrame = FNetPacketCodec::EncodeFrame(Header, EncodedRoom.GetValue());
    return SocketChannel.SendTo(WireFrame, TargetEndpoint.IpAddress, TargetEndpoint.Port);
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
 * @brief 非阻塞轮询接收缓冲队列、应答探测请求并清理过期房间
 * @param DeltaTimeSeconds 距离上一帧的秒数增量
 */
void FLanBeaconBroadcaster::Tick(float DeltaTimeSeconds) {
    MonotonicClockSeconds += FMath::Max(0.0, static_cast<double>(DeltaTimeSeconds));
    if (!IsActive()) {
        return;
    }

    PumpIncomingDatagrams();

    if (Role == EGameNetLanRole::Discovering) {
        const TArray<FString> ExpiredIds = DiscoveryTable.PruneExpiredRooms(MonotonicClockSeconds);
        for (const FString& ExpiredId : ExpiredIds) {
            RoomExpiredDelegate.Broadcast(ExpiredId);
        }
    }
}

/**
 * @brief 非阻塞排空当前 UDP 通道中的所有到达数据报并按协议派发
 */
void FLanBeaconBroadcaster::PumpIncomingDatagrams() {
    for (int32 ProcessedCount = 0; ProcessedCount < MAX_PUMP_DATAGRAMS_PER_TICK; ++ProcessedCount) {
        const TOptional<FLanReceivedDatagram> Datagram = SocketChannel.TryReceiveDatagram();
        if (!Datagram.IsSet()) {
            break;
        }

        const TGameStatusResult<FNetPacketHeader> DecodedHeader = FNetPacketCodec::TryDecodeHeader(Datagram->Bytes);
        if (DecodedHeader.IsFailure() || !FNetPacketCodec::IsValidHeader(DecodedHeader.GetValue(), MAX_BEACON_PAYLOAD_BYTES)) {
            continue;
        }

        const uint32 ExpectedWireBytes =
            static_cast<uint32>(FNetPacketHeader::WIRE_HEADER_BYTES) + DecodedHeader.GetValue().PayloadLength;
        if (static_cast<uint32>(Datagram->Bytes.Num()) != ExpectedWireBytes) {
            continue;
        }

        if (Role == EGameNetLanRole::Hosting && DecodedHeader.GetValue().MessageId == LAN_DISCOVERY_PROBE_MSG_ID) {
            ReplyToProbe(DecodedHeader.GetValue(), Datagram->SenderIp, Datagram->SenderPort);
        } else if (Role == EGameNetLanRole::Discovering && DecodedHeader.GetValue().MessageId == LAN_ROOM_ANNOUNCE_MSG_ID) {
            const TConstArrayView<uint8> PayloadView(
                Datagram->Bytes.GetData() + FNetPacketHeader::WIRE_HEADER_BYTES,
                static_cast<int32>(DecodedHeader.GetValue().PayloadLength)
            );
            HandleAnnouncementPayload(DecodedHeader.GetValue(), PayloadView, Datagram->SenderIp);
        }
    }
}

/**
 * @brief 房主处理客户端探测请求并原路单播回传房间宣告帧
 * @param ProbeHeader 客户端探测报文头
 * @param SenderIp 客户端来源 IP
 * @param SenderPort 客户端来源端口
 */
void FLanBeaconBroadcaster::ReplyToProbe(
    const FNetPacketHeader& ProbeHeader,
    const FString& SenderIp,
    int32 SenderPort
) {
    const TGameStatusResult<TArray<uint8>> EncodedRoom = JsonSerializer.SerializeStruct(HostedRoom);
    if (EncodedRoom.IsFailure() || static_cast<uint32>(EncodedRoom.GetValue().Num()) > MAX_BEACON_PAYLOAD_BYTES) {
        return;
    }

    FNetPacketHeader ReplyHeader;
    ReplyHeader.MessageId = LAN_ROOM_ANNOUNCE_MSG_ID;
    ReplyHeader.SequenceId = ProbeHeader.SequenceId;

    const TArray<uint8> WireFrame = FNetPacketCodec::EncodeFrame(ReplyHeader, EncodedRoom.GetValue());
    SocketChannel.SendTo(WireFrame, SenderIp, SenderPort);
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
    const TGameStatusResult<FGameNetRoomDescriptor> DecodedRoom =
        JsonSerializer.DeserializeStruct<FGameNetRoomDescriptor>(Payload);
    if (DecodedRoom.IsFailure()) {
        return;
    }

    const TOptional<FGameNetRoomDescriptor> UpsertedRoom = DiscoveryTable.UpsertDiscoveredRoom(
        DecodedRoom.GetValue(),
        SenderIp,
        AnnounceHeader.SequenceId,
        MonotonicClockSeconds
    );
    if (UpsertedRoom.IsSet()) {
        RoomDiscoveredDelegate.Broadcast(*UpsertedRoom);
    }
}

