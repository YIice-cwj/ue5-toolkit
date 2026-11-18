// Copyright Epic Games, Inc. All Rights Reserved.

#include "Message/GameplayMessageTypes.h"

#include "Async/Async.h"
#include "Message/GameplayMessageSubsystem.h"

/**
 * @brief 注册内部结构体析构函数，保证生命周期结束时自动安全注销
 */
FGameplayMessageListenerHandle::FListenerRegistration::~FListenerRegistration() {
    Unregister();
}

/**
 * @brief 执行退订逻辑，若在非游戏线程触发则自动切回游戏线程注销（原子 exchange 保证严格单次退订）
 */
void FGameplayMessageListenerHandle::FListenerRegistration::Unregister() {
    const int64 IdToUnregister = ListenerId.exchange(0, std::memory_order_acq_rel);
    if (IdToUnregister != 0) {
        const FGameplayTag ChannelToUnregister = Channel;
        TWeakObjectPtr<UGameplayMessageSubsystem> SubsystemToNotify = SubsystemWeak;

        SubsystemWeak.Reset();
        Channel = FGameplayTag();

        if (IsInGameThread()) {
            if (UGameplayMessageSubsystem* Subsystem = SubsystemToNotify.Get()) {
                Subsystem->UnregisterListener(ChannelToUnregister, IdToUnregister);
            }
        } else {
            AsyncTask(ENamedThreads::GameThread, [SubsystemToNotify, ChannelToUnregister, IdToUnregister]() {
                if (UGameplayMessageSubsystem* Subsystem = SubsystemToNotify.Get()) {
                    Subsystem->UnregisterListener(ChannelToUnregister, IdToUnregister);
                }
            });
        }
    }
}

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
 * @brief 检查句柄当前是否有效
 * @return 有效返回 true
 */
bool FGameplayMessageListenerHandle::IsValid() const noexcept {
    return Registration.IsValid() && Registration->ListenerId.load(std::memory_order_acquire) != 0 && Registration->SubsystemWeak.IsValid();
}

/**
 * @brief 内部私有构造函数
 * @param InSubsystem 消息子系统实例
 * @param InChannel 注册通道
 * @param InId 注册监听器 ID
 */
FGameplayMessageListenerHandle::FGameplayMessageListenerHandle(
    UGameplayMessageSubsystem* InSubsystem,
    FGameplayTag InChannel,
    int64 InId
) {
    Registration = MakeShared<FListenerRegistration>();
    Registration->SubsystemWeak = InSubsystem;
    Registration->Channel = InChannel;
    Registration->ListenerId.store(InId, std::memory_order_release);
}
