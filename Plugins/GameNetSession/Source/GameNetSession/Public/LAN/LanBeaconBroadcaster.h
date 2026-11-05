/**
 * @file LanBeaconBroadcaster.h
 * @brief 协调 UDP 通道、协议编解码器与房间发现表的局域网信标广播及发现控制器
 */

#pragma once

#include "CoreMinimal.h"
#include "LAN/LanBeaconTypes.h"
#include "LAN/LanRoomDiscoveryTable.h"
#include "LAN/LanUdpSocketChannel.h"
#include "Packet/NetPacketTypes.h"
#include "Serializer/JsonNetSerializer.h"
#include "Types/GameNetSessionTypes.h"

/**
 * @brief 局域网房间信标协议协调器（组合底层 UDP 通道、帧编解码器与发现缓存表）
 */
class GAMENETSESSION_API FLanBeaconBroadcaster final {
public:
    static constexpr uint32 LAN_DISCOVERY_PROBE_MSG_ID = 0xFFFFFF10;
    static constexpr uint32 LAN_ROOM_ANNOUNCE_MSG_ID = 0xFFFFFF11;
    static constexpr int32 DEFAULT_BEACON_PORT = 15777;
    static constexpr uint32 MAX_BEACON_PAYLOAD_BYTES = 4096;
    static constexpr float DEFAULT_ROOM_EXPIRATION_SECONDS = 8.0f;

private:
    static constexpr int32 MAX_PUMP_DATAGRAMS_PER_TICK = 64;

public:
    /**
     * @brief 构造局域网信标广播器
     * @param InRoomExpirationSeconds 发现端房间无响应过期剔除阈值（秒）
     */
    explicit FLanBeaconBroadcaster(float InRoomExpirationSeconds = DEFAULT_ROOM_EXPIRATION_SECONDS) noexcept;

    /**
     * @brief 析构时自动停止并释放 UDP 通道
     */
    ~FLanBeaconBroadcaster();

    FLanBeaconBroadcaster(const FLanBeaconBroadcaster&) = delete;
    FLanBeaconBroadcaster& operator=(const FLanBeaconBroadcaster&) = delete;
    FLanBeaconBroadcaster(FLanBeaconBroadcaster&&) = delete;
    FLanBeaconBroadcaster& operator=(FLanBeaconBroadcaster&&) = delete;

    /**
     * @brief 以房主身份启动 UDP 监听信标，自动应答局域网探测请求
     * @param InHostedRoom 房间描述元数据（RoomId 为空时自动生成唯一 GUID）
     * @param BindPort 绑定的本地 UDP 端口（传 0 由操作系统分配空闲端口）
     * @param BindIp 绑定的本地网卡地址（默认 0.0.0.0）
     * @return 通道打开成功返回 true
     */
    bool StartHosting(
        const FGameNetRoomDescriptor& InHostedRoom,
        int32 BindPort = DEFAULT_BEACON_PORT,
        const FString& BindIp = TEXT("0.0.0.0")
    );

    /**
     * @brief 动态刷新房主广播的房间人数或地图等描述信息
     * @param UpdatedRoom 最新房间描述信息
     */
    void UpdateHostedRoom(const FGameNetRoomDescriptor& UpdatedRoom);

    /**
     * @brief 以客户端搜索者身份启动非阻塞 UDP 发现端口
     * @param BindPort 绑定的本地 UDP 端口（默认 0 自动分配临时端口）
     * @param BindIp 绑定的本地网卡地址（默认 0.0.0.0）
     * @return 通道打开成功返回 true
     */
    bool StartDiscovery(int32 BindPort = 0, const FString& BindIp = TEXT("0.0.0.0"));

    /**
     * @brief 关闭 UDP 通道并重置发现表与工作角色
     */
    void Stop();

    /**
     * @brief 向局域网广播地址或指定单播地址发送房间探测报文
     * @param TargetEndpoint 目标 UDP 地址与端口
     * @return 数据报发送成功返回 true
     */
    bool SendDiscoveryProbe(const FGameNetLanEndpoint& TargetEndpoint = FGameNetLanEndpoint{});

    /**
     * @brief 房主主动向目标地址推送一次房间宣告数据报
     * @param TargetEndpoint 目标 UDP 地址与端口
     * @return 数据报发送成功返回 true
     */
    bool BroadcastAnnouncement(const FGameNetLanEndpoint& TargetEndpoint = FGameNetLanEndpoint{});

    /**
     * @brief 事件驱动等待底层 UDP 通道可读就绪
     * @param MaxWaitSeconds 最大等待秒数上限
     * @return 超时前检测到可读数据报到达返回 true
     */
    bool WaitForIncomingData(double MaxWaitSeconds) const;

    /**
     * @brief 非阻塞轮询接收缓冲队列、应答探测请求并清理过期房间
     * @param DeltaTimeSeconds 距离上一帧的秒数增量
     */
    void Tick(float DeltaTimeSeconds);

    /**
     * @brief 查询当前信标所处工作角色
     * @return 局域网信标角色枚举
     */
    [[nodiscard]] EGameNetLanRole GetRole() const noexcept {
        return Role;
    }

    /**
     * @brief 查询信标当前是否处于活跃已绑定状态
     * @return 活跃返回 true
     */
    [[nodiscard]] bool IsActive() const noexcept {
        return Role != EGameNetLanRole::Inactive && SocketChannel.IsOpen();
    }

    /**
     * @brief 获取底层 UDP 通道实际绑定的本地端口号
     * @return 实际绑定端口号；未启动时返回 0
     */
    [[nodiscard]] int32 GetBoundPort() const noexcept {
        return SocketChannel.GetBoundPort();
    }

    /**
     * @brief 获取当前房主正在广播的房间元数据
     * @return 房主房间描述符常引用
     */
    [[nodiscard]] const FGameNetRoomDescriptor& GetHostedRoom() const noexcept {
        return HostedRoom;
    }

    /**
     * @brief 获取当前所有未过期的已发现局域网房间快照列表
     * @return 房间描述符数组
     */
    [[nodiscard]] TArray<FGameNetRoomDescriptor> GetDiscoveredRooms() const {
        return DiscoveryTable.GetRoomsSnapshot();
    }

    /**
     * @brief 房间首次发现或元数据刷新事件广播
     * @return 多播委托引用
     */
    FOnLanRoomDiscovered& OnRoomDiscovered() noexcept {
        return RoomDiscoveredDelegate;
    }

    /**
     * @brief 房间超时失联剔除事件广播
     * @return 多播委托引用
     */
    FOnLanRoomExpired& OnRoomExpired() noexcept {
        return RoomExpiredDelegate;
    }

private:
    /**
     * @brief 非阻塞排空当前 UDP 通道中的所有到达数据报并按协议派发
     */
    void PumpIncomingDatagrams();

    /**
     * @brief 房主处理客户端探测请求并原路单播回传房间宣告帧
     * @param ProbeHeader 客户端探测报文头
     * @param SenderIp 客户端来源 IP
     * @param SenderPort 客户端来源端口
     */
    void ReplyToProbe(const FNetPacketHeader& ProbeHeader, const FString& SenderIp, int32 SenderPort);

    /**
     * @brief 客户端处理房主宣告负载并同步更新发现缓存表与事件委托
     * @param AnnounceHeader 宣告报文头
     * @param Payload UTF-8 JSON 负载切片
     * @param SenderIp 房主来源 IP
     */
    void HandleAnnouncementPayload(
        const FNetPacketHeader& AnnounceHeader,
        TConstArrayView<uint8> Payload,
        const FString& SenderIp
    );

private:
    FLanUdpSocketChannel SocketChannel;
    FLanRoomDiscoveryTable DiscoveryTable;
    FJsonNetSerializer JsonSerializer{false};
    EGameNetLanRole Role{EGameNetLanRole::Inactive};
    FGameNetRoomDescriptor HostedRoom;
    double MonotonicClockSeconds{0.0};
    FOnLanRoomDiscovered RoomDiscoveredDelegate;
    FOnLanRoomExpired RoomExpiredDelegate;
};

