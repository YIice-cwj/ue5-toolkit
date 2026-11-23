// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dispatch/NetMessageDispatcher.h"

/**
 * @brief 注册针对指定协议 ID 的原始报文多播监听处理器
 * @param MessageId 监听的协议 ID
 * @param Handler 报文到达回调闭包
 * @return 唯一订阅句柄 ID，无效闭包返回 0
 */
int64 FNetMessageDispatcher::RegisterHandler(uint32 MessageId, FNetRawMessageHandler Handler) {
    if (!Handler) {
        return 0;
    }

    const int64 AssignedId = ++NextHandlerId;
    TArray<FHandlerSlot>& Slots = MessageHandlers.FindOrAdd(MessageId);
    FHandlerSlot Slot;
    Slot.HandlerId = AssignedId;
    Slot.Callback = MoveTemp(Handler);
    Slots.Add(MoveTemp(Slot));
    return AssignedId;
}

/**
 * @brief 注销指定协议 ID 与句柄 ID 对应的处理器
 * @param MessageId 协议 ID
 * @param HandlerId 注册时返回的句柄 ID
 * @return 成功找到并移除返回 true，未找到返回 false
 */
bool FNetMessageDispatcher::UnregisterHandler(uint32 MessageId, int64 HandlerId) {
    TArray<FHandlerSlot>* Slots = MessageHandlers.Find(MessageId);
    if (Slots == nullptr) {
        return false;
    }

    const int32 FoundIndex = Slots->IndexOfByPredicate([HandlerId](const FHandlerSlot& Candidate) {
        return Candidate.HandlerId == HandlerId;
    });
    if (FoundIndex == INDEX_NONE) {
        return false;
    }

    Slots->RemoveAt(FoundIndex, EAllowShrinking::No);
    if (Slots->IsEmpty()) {
        MessageHandlers.Remove(MessageId);
    }
    return true;
}

/**
 * @brief 按快照隔离方式向指定协议 ID 的所有已注册监听器多播派发数据包（零错误对象堆分配）
 * @param Header 报文头
 * @param Payload 负载二进制切片
 * @return 实际被调用的常驻处理器数量（无监听器返回 0）
 */
int32 FNetMessageDispatcher::DispatchPacket(const FNetPacketHeader& Header, TConstArrayView<uint8> Payload) {
    const TArray<FHandlerSlot>* Slots = MessageHandlers.Find(Header.MessageId);
    if (Slots == nullptr || Slots->IsEmpty()) {
        return 0;
    }

    TArray<FHandlerSlot, TInlineAllocator<4>> Snapshot;
    Snapshot.Append(*Slots);
    for (const FHandlerSlot& Slot : Snapshot) {
        Slot.Callback(Header, Payload);
    }
    return Snapshot.Num();
}

/**
 * @brief 清空所有已注册的常驻协议监听器
 */
void FNetMessageDispatcher::ClearHandlers() {
    MessageHandlers.Reset();
}

/**
 * @brief 查询指定协议 ID 当前挂载的常驻监听器数量
 * @param MessageId 协议 ID
 * @return 监听器数量
 */
int32 FNetMessageDispatcher::GetHandlerCount(uint32 MessageId) const noexcept {
    const TArray<FHandlerSlot>* Slots = MessageHandlers.Find(MessageId);
    return Slots != nullptr ? Slots->Num() : 0;
}
