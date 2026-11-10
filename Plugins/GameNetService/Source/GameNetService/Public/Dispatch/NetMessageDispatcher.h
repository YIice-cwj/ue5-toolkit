/**
 * @file NetMessageDispatcher.h
 * @brief 按业务协议 ID 多播订阅、注销与快照迭代安全派发路由器
 */

#pragma once

#include "CoreMinimal.h"
#include "Dispatch/NetDispatchTypes.h"
#include "Types/TGameResult.h"

/**
 * @brief 常驻网络协议多播监听器注册表与快照安全路由器
 */
class GAMENETSERVICE_API FNetMessageDispatcher final {
private:
    struct FHandlerSlot {
        int64 HandlerId{0};
        FNetRawMessageHandler Callback;
    };

public:
    FNetMessageDispatcher() = default;

    /**
     * @brief 注册针对指定协议 ID 的原始报文多播监听处理器
     * @param MessageId 监听的协议 ID
     * @param Handler 报文到达回调闭包
     * @return 唯一订阅句柄 ID（可用于精确注销），无效闭包返回 0
     */
    [[nodiscard]] int64 RegisterHandler(uint32 MessageId, FNetRawMessageHandler Handler);

    /**
     * @brief 注销指定协议 ID 与句柄 ID 对应的处理器
     * @param MessageId 协议 ID
     * @param HandlerId 注册时返回的句柄 ID
     * @return 成功注销返回 Success，未找到对应处理器返回 Failure(2005)
     */
    TGameStatusResult<void> UnregisterHandler(uint32 MessageId, int64 HandlerId);

    /**
     * @brief 按快照隔离方式向指定协议 ID 的所有已注册监听器多播派发数据包
     * @param Header 报文头
     * @param Payload 负载二进制切片
     * @return 至少命中一个处理器返回 Success，无监听器返回 Failure(2006)
     */
    TGameStatusResult<void> DispatchPacket(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload);

    /**
     * @brief 清空所有已注册的常驻协议监听器
     */
    void ClearHandlers();

    /**
     * @brief 查询指定协议 ID 当前挂载的常驻监听器数量
     * @param MessageId 协议 ID
     * @return 监听器数量
     */
    [[nodiscard]] int32 GetHandlerCount(uint32 MessageId) const noexcept;

private:
    int64 NextHandlerId{0};
    TMap<uint32, TArray<FHandlerSlot>> MessageHandlers;
};

