/**
 * @file LanRoomDiscoveryTable.h
 * @brief 局域网发现房间去重缓存、多房主广播探测 RTT 测算与静默过期剔除表
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/GameNetSessionTypes.h"

/**
 * @brief 管理已发现局域网房间列表、多房主探测往返时延计算及有界防洪泛淘汰的纯内存表
 */
class GAMENETSESSION_API FLanRoomDiscoveryTable final {
private:
    struct FDiscoveredRoomRecord {
        FGameNetRoomDescriptor Descriptor;
        double LastSeenTimeSeconds{0.0};
    };

public:
    static constexpr float DEFAULT_EXPIRATION_SECONDS = 8.0f;
    static constexpr float MIN_EXPIRATION_FLOOR_SECONDS = 0.5f;
    static constexpr int32 MAX_DISCOVERED_ROOMS = 256;
    static constexpr int32 MAX_PENDING_PROBES = 64;

public:
    /**
     * @brief 构造房间发现缓存表
     * @param InExpirationSeconds 房间无宣告刷新过期剔除秒数阈值
     */
    explicit FLanRoomDiscoveryTable(float InExpirationSeconds = DEFAULT_EXPIRATION_SECONDS) noexcept;

    /**
     * @brief 分配下一个非零局域网探测序列号并记录发出时间戳（自动维持有界探测表容量）
     * @param SentClockSeconds 探测发出时刻秒数
     * @return 分配的非零探测序列号
     */
    [[nodiscard]] uint32 AllocateAndRecordProbe(double SentClockSeconds);

    /**
     * @brief 校验、防注入清洗来源地址、测算多房主广播探测 RTT 并原地插入或更新发现房间记录
     * @param IncomingRoom 解码出的房间描述符
     * @param SenderIp 数据报发送方 IPv4 字符串
     * @param EchoedSequenceId 宣告报文头回显的探测序列号（同一广播序号可被多个房主共享测算 RTT）
     * @param CurrentClockSeconds 当前单调时钟秒数
     * @return 房间字段合法且未超容量上限时返回归一化后的房间描述符，否则返回空值
     */
    [[nodiscard]] TOptional<FGameNetRoomDescriptor> UpsertDiscoveredRoom(
        FGameNetRoomDescriptor IncomingRoom,
        const FString& SenderIp,
        uint32 EchoedSequenceId,
        double CurrentClockSeconds
    );

    /**
     * @brief 扫描并移除所有超过过期阈值的房间记录与过期探测时间戳，就地写入外部复用数组（零额外堆分配）
     * @param CurrentClockSeconds 当前单调时钟秒数
     * @param OutExpiredIds 本次被剔除的所有过期 RoomId 目标数组（就地清空并复用已有容量）
     */
    void PruneExpiredRooms(double CurrentClockSeconds, TArray<FString>& OutExpiredIds);

    /**
     * @brief 获取当前未过期局域网房间的描述符快照并就地写入外部复用数组
     * @param OutSnapshot 目标接收数组（就地清空并复用已有容量）
     */
    void GetRoomsSnapshot(TArray<FGameNetRoomDescriptor>& OutSnapshot) const;

    /**
     * @brief 清空所有已发现房间缓存与挂起探测时间记录
     */
    void Reset();

    /**
     * @brief 查询当前活跃房间条目总数
     * @return 房间数量
     */
    [[nodiscard]] int32 GetRoomCount() const noexcept {
        return DiscoveredRooms.Num();
    }

private:
    /**
     * @brief 校验房主主机地址字段不含空白、控制符或 Travel URL 选项注入字符
     * @param HostToken 待校验的主机地址字符串
     * @return 合法返回 true
     */
    [[nodiscard]] static bool IsSafeHostAddress(const FString& HostToken) noexcept;

private:
    TMap<FString, FDiscoveredRoomRecord> DiscoveredRooms;
    TMap<uint32, double> PendingProbeSendTimes;
    float ExpirationSeconds{DEFAULT_EXPIRATION_SECONDS};
    uint32 NextProbeSequenceId{0};
};
