/**
 * @file FObserverBase.cpp
 * @brief 统一监听基类实现：注册并响应式监听配置中心启停开关
 */

#include "Listener/FObserverBase.h"

FObserverBase::FObserverBase(const FName& InObserverName)
    : ObserverName(InObserverName),
      EnableConfigKey(BuildEnableConfigKey(InObserverName)),
      EnabledFlag(MakeShared<std::atomic<bool>>(true)) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    Registry.RegisterConfig<bool>(EnableConfigKey, true);
    EnabledFlag->store(Registry.GetConfig<bool>(EnableConfigKey, true), std::memory_order_relaxed);
    TWeakPtr<std::atomic<bool>> WeakFlag = EnabledFlag;
    EnableWatchSubscription = Registry.WatchConfig<bool>(EnableConfigKey, [WeakFlag](const bool& bNewValue) {
        if (TSharedPtr<std::atomic<bool>> Pinned = WeakFlag.Pin()) {
            Pinned->store(bNewValue, std::memory_order_relaxed);
        }
    });
}

FObserverBase::~FObserverBase() {
    if (EnableWatchSubscription.IsValid()) {
        EnableWatchSubscription->Reset();
    }
}

void FObserverBase::SetEnabled(bool bInEnabled) {
    EnabledFlag->store(bInEnabled, std::memory_order_relaxed);
    FConfigRegistry& Registry = FConfigRegistry::Get();
    Registry.RegisterConfig<bool>(EnableConfigKey, bInEnabled);
    Registry.SetConfig<bool>(EnableConfigKey, bInEnabled);
}

void FObserverBase::Broadcast() {
    if (!IsEnabled()) {
        return;
    }
    Version.fetch_add(1, std::memory_order_relaxed);
    Notify();
}

FName FObserverBase::BuildEnableConfigKey(const FName& InObserverName) {
    return FName(*FString::Printf(TEXT("Listener.%s.Enabled"), *InObserverName.ToString()));
}

