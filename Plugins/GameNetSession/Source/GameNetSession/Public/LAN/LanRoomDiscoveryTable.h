/**
 * @file LanRoomDiscoveryTable.h
 * @brief 局域网发现房间去重缓存、探测序列号 RTT 测算与静默过期剔除表
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/GameNetSessionTypes.h"

/**
 * @brief 管理已发现局域网房间列表、探测往返时延计算及超时淘汰的纯内存表
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

public:
    /**
     * @brief 构造房间发现缓存表
     * @param InExpirationSeconds 房间无宣告刷新过期剔除秒数阈值
     */
    explicit FLanRoomDiscoveryTable(float InExpirationSeconds = DEFAULT_EXPIRATION_SECONDS) noexcept;

    /**
     * @brief 分配下一个非零局域网探测序列号并记录发出时间戳
     * @param SentClockSeconds 探测发出时刻秒数
     * @return 分配的非零探测序列号
     */
    [[nodiscard]] uint32 AllocateAndRecordProbe(double SentClockSeconds);

    /**
     * @brief 校验、补全来源地址、计算探测回显 RTT 并原地插入或更新发现房间记录
     * @param IncomingRoom 解码出的房间描述符
     * @param SenderIp 数据报发送方 IPv4 字符串
     * @param EchoedSequenceId 宣告报文头回显的探测序列号
     * @param CurrentClockSeconds 当前单调时钟秒数
     * @return 房间字段合法时返回归一化后的房间描述符，字段非法时返回空值
     */
    [[nodiscard]] TOptional<FGameNetRoomDescriptor> UpsertDiscoveredRoom(
        FGameNetRoomDescriptor IncomingRoom,
        const FString& SenderIp,
        uint32 EchoedSequenceId,
        double CurrentClockSeconds
    );

    /**
     * @brief 扫描并移除所有超过过期阈值的房间记录
     * @param CurrentClockSeconds 当前单调时钟秒数
     * @return 本次被剔除的所有过期 RoomId 列表
     */
    [[nodiscard]] TArray<FString> PruneExpiredRooms(double CurrentClockSeconds);

    /**
     * @brief 获取当前所有未过期局域网房间的描述符快照列表
     * @return 房间描述符数组
     */
    [[nodiscard]] TArray<FGameNetRoomDescriptor> GetRoomsSnapshot() const;

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
    TMap<FString, FDiscoveredRoomRecord> DiscoveredRooms;
    TMap<uint32, double> PendingProbeSendTimes;
    float ExpirationSeconds{DEFAULT_EXPIRATION_SECONDS};
    uint32 NextProbeSequenceId{0};
};

