/**
 * @file FMessageListenerRegistry.h
 * @brief 消息总线通道监听器注册表声明（职责：精确/父子层级路由、重入安全活跃标记与运行纪元管理）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Message/GameplayMessageTypes.h"
#include "Templates/SharedPointer.h"

namespace Game::Message::Private {

using FRawMessageCallback = TFunction<void(FGameplayTag, const UScriptStruct*, const void*)>;

/**
 * @brief 通道监听器注册表（仅在 GameThread 访问）
 */
class FMessageListenerRegistry final {
public:
    /**
     * @brief 回调共享外壳（提供零额外分配的重入退订失活守卫，且保证回调闭包仅在主线程析构）
     */
    struct FCallbackBox {
        /**
         * @brief 原地移动构造活跃回调外壳
         * @param InCallback 底层类型擦除回调闭包
         */
        explicit FCallbackBox(FRawMessageCallback InCallback)
            : Callback(MoveTemp(InCallback)) {
        }

        FRawMessageCallback Callback;
        bool bActive = true;
    };

    /**
     * @brief 注册监听器返回的句柄签发凭证
     */
    struct FRegisterReceipt {
        int64 ListenerId = 0;
        TWeakPtr<const void> ActiveEpochWeak;
    };

    using FCallbackSnapshot = TArray<TSharedPtr<FCallbackBox, ESPMode::NotThreadSafe>, TInlineAllocator<16>>;

public:
    FMessageListenerRegistry();

    FMessageListenerRegistry(const FMessageListenerRegistry&) = delete;
    FMessageListenerRegistry& operator=(const FMessageListenerRegistry&) = delete;
    FMessageListenerRegistry(FMessageListenerRegistry&&) = delete;
    FMessageListenerRegistry& operator=(FMessageListenerRegistry&&) = delete;

    /**
     * @brief 开启新的子系统运行纪元（Initialize 时调用，使新签发句柄绑定至最新纪元）
     */
    void BeginEpoch();

    /**
     * @brief 注册一个类型擦除通道监听器
     * @param Channel 目标通道标签
     * @param StructType 期望匹配的反射结构体元数据指针（为空表示通配）
     * @param Callback 底层类型擦除回调闭包
     * @param MatchRule 精确或父子前缀匹配规则
     * @return 监听器 ID 与当前运行纪元弱引用
     */
    [[nodiscard]] FRegisterReceipt Register(
        FGameplayTag Channel,
        const UScriptStruct* StructType,
        FRawMessageCallback Callback,
        EGameplayMessageMatch MatchRule
    );

    /**
     * @brief 按通道与唯一 ID 注销监听器，并立即置位对应 CallbackBox 失活标记以防同轮广播误触发
     * @param Channel 目标通道标签
     * @param ListenerId 监听器唯一 ID
     */
    void Unregister(FGameplayTag Channel, int64 ListenerId);

    /**
     * @brief 清空全部通道监听器、失活全部在途快照外壳并作废当前运行纪元
     * @return 本次清空涉及的通道总数
     */
    int32 ClearAll();

    /**
     * @brief 检查向目标通道广播时是否存在至少一个可命中的活跃监听器（含祖先通道 PartialMatch）
     * @param Channel 目标广播通道
     * @return 存在匹配监听器返回 true
     */
    [[nodiscard]] bool HasListener(FGameplayTag Channel) const;

    /**
     * @brief 收集目标通道与反射类型匹配的全部活跃监听器回调快照（零闭包深拷贝）
     * @param Channel 目标广播通道
     * @param StructType 广播载荷反射结构体类型
     * @param OutCallbacks 输出的栈内联回调外壳快照列表
     */
    void CollectMatchingCallbacks(
        FGameplayTag Channel,
        const UScriptStruct* StructType,
        FCallbackSnapshot& OutCallbacks
    ) const;

    /**
     * @brief 获取当前注册的活跃监听器总数（O(1)）
     * @return 监听器总数
     */
    [[nodiscard]] int32 GetListenerCount() const noexcept {
        return TotalListenerCount;
    }

private:
    struct FListenerEntry {
        int64 Id = 0;
        EGameplayMessageMatch MatchRule = EGameplayMessageMatch::ExactMatch;
        bool bWildcard = false;
        TWeakObjectPtr<const UScriptStruct> ExpectedStructType;
        TSharedPtr<FCallbackBox, ESPMode::NotThreadSafe> CallbackBox;
    };

    struct FChannelListenerList {
        TArray<FListenerEntry> Listeners;
        int32 PartialMatchCount = 0;
    };

    /**
     * @brief 判断监听条目与给定载荷结构体类型是否兼容（通配、精确一致或子类；单次弱引用解析）
     * @param Entry 监听条目
     * @param StructType 广播载荷反射结构体类型
     * @return 类型兼容返回 true
     */
    [[nodiscard]] static bool IsEntryCompatible(const FListenerEntry& Entry, const UScriptStruct* StructType) noexcept;

    /**
     * @brief 通过 GameplayTag 树节点指针链零堆分配遍历目标通道的全部严格祖先通道
     * @tparam TVisitor 接收 const FChannelListenerList& 并返回 bool（返回 false 提前停止）的访问者
     * @param Channel 目标子通道
     * @param Visitor 祖先通道列表访问回调
     */
    template <typename TVisitor>
    void ForEachParentChannel(FGameplayTag Channel, TVisitor&& Visitor) const;

private:
    TMap<FGameplayTag, FChannelListenerList> ListenerMap;
    TSharedPtr<uint8> ActiveEpoch;
    int64 NextListenerId = 0;
    int32 PartialMatchListenerCount = 0;
    int32 TotalListenerCount = 0;
};

}  // namespace Game::Message::Private
