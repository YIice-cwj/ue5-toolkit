/**
 * @file FGameplayMessageListenerHandle.cpp
 * @brief 消息总线监听订阅 RAII 句柄实现（无数据竞争不可变状态与纪元守卫跨线程注销）
 */

#include "Message/FGameplayMessageListenerHandle.h"

#include <atomic>

#include "Async/Async.h"
#include "Message/GameplayMessageSubsystem.h"

/**
 * @brief 句柄底层共享注册态（除原子 ListenerId 外全部构造期只读，彻底消除跨线程读写竞争）
 */
struct FGameplayMessageListenerHandle::FListenerRegistration {
public:
    /**
     * @brief 构造共享注册态
     * @param InSubsystem 所属消息子系统
     * @param InChannel 注册通道
     * @param InId 监听器唯一 ID
     * @param InActiveEpoch 所属子系统活跃纪元弱引用
     */
    FListenerRegistration(
        UGameplayMessageSubsystem* InSubsystem,
        FGameplayTag InChannel,
        int64 InId,
        TWeakPtr<const void> InActiveEpoch
    ) noexcept
        : SubsystemWeak(InSubsystem)
        , Channel(InChannel)
        , ActiveEpochWeak(MoveTemp(InActiveEpoch))
        , ListenerId(InId) {
    }

    FListenerRegistration(const FListenerRegistration&) = delete;
    FListenerRegistration& operator=(const FListenerRegistration&) = delete;
    FListenerRegistration(FListenerRegistration&&) = delete;
    FListenerRegistration& operator=(FListenerRegistration&&) = delete;

    /**
     * @brief 析构时确保自动退订
     */
    ~FListenerRegistration() {
        Unregister();
    }

    /**
     * @brief 执行严格单次退订，非主线程调用时安全切回 GameThread 并受纪元守卫保护
     */
    void Unregister() {
        const int64 IdToUnregister = ListenerId.exchange(0, std::memory_order_acq_rel);
        if (IdToUnregister == 0 || !ActiveEpochWeak.IsValid()) {
            return;
        }

        if (IsInGameThread()) {
            if (UGameplayMessageSubsystem* Subsystem = SubsystemWeak.Get()) {
                Subsystem->UnregisterListenerInternal(Channel, IdToUnregister);
            }
            return;
        }

        const TWeakObjectPtr<UGameplayMessageSubsystem> SubsystemToNotify = SubsystemWeak;
        const FGameplayTag ChannelToUnregister = Channel;
        const TWeakPtr<const void> EpochToVerify = ActiveEpochWeak;
        AsyncTask(
            ENamedThreads::GameThread,
            [SubsystemToNotify, ChannelToUnregister, IdToUnregister, EpochToVerify]() {
                if (!EpochToVerify.IsValid()) {
                    return;
                }
                if (UGameplayMessageSubsystem* Subsystem = SubsystemToNotify.Get()) {
                    Subsystem->UnregisterListenerInternal(ChannelToUnregister, IdToUnregister);
                }
            }
        );
    }

    /**
     * @brief 检查底层注册当前是否仍处于有效活跃状态
     * @return 未退订且所属子系统运行纪元未过期返回 true
     */
    [[nodiscard]] bool IsActive() const noexcept {
        return ListenerId.load(std::memory_order_acquire) != 0 && ActiveEpochWeak.IsValid();
    }

private:
    const TWeakObjectPtr<UGameplayMessageSubsystem> SubsystemWeak;
    const FGameplayTag Channel;
    const TWeakPtr<const void> ActiveEpochWeak;
    std::atomic<int64> ListenerId{0};
};

/**
 * @brief 手动注销监听并清空句柄
 */
void FGameplayMessageListenerHandle::Unregister() {
    if (Registration.IsValid()) {
        Registration->Unregister();
        Registration.Reset();
    }
}

/**
 * @brief 检查句柄当前是否指向活跃监听（线程安全，子系统 Deinitialize 后立即失效）
 * @return 有效返回 true
 */
bool FGameplayMessageListenerHandle::IsValid() const noexcept {
    return Registration.IsValid() && Registration->IsActive();
}

/**
 * @brief 内部私有构造函数（仅由 UGameplayMessageSubsystem 签发）
 * @param InSubsystem 消息子系统实例
 * @param InChannel 注册通道
 * @param InId 注册监听器 ID
 * @param InActiveEpoch 所属子系统当前运行纪元弱引用
 */
FGameplayMessageListenerHandle::FGameplayMessageListenerHandle(
    UGameplayMessageSubsystem* InSubsystem,
    FGameplayTag InChannel,
    int64 InId,
    TWeakPtr<const void> InActiveEpoch
)
    : Registration(MakeShared<FListenerRegistration>(InSubsystem, InChannel, InId, MoveTemp(InActiveEpoch))) {
}
