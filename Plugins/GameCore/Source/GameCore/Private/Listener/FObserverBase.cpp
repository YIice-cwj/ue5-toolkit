/**
 * @file FObserverBase.cpp
 * @brief 统一监听基类实现：以配置中心作为启停开关唯一真源
 */

#include "Listener/FObserverBase.h"

FObserverBase::FObserverBase(const FName& InObserverName)
    : ObserverName(InObserverName),
      EnableConfigKey(BuildEnableConfigKey(InObserverName)) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    Registry.RegisterConfig<bool>(EnableConfigKey, true);
    bCachedEnabled.store(Registry.GetConfig<bool>(EnableConfigKey, true), std::memory_order_relaxed);
    EnableWatchSubscription = Registry.WatchConfig<bool>(
        EnableConfigKey,
        [this](const bool& bNewEnabled) {
            bCachedEnabled.store(bNewEnabled, std::memory_order_relaxed);
        }
    );
}

FObserverBase::~FObserverBase() {
    if (EnableWatchSubscription.IsValid()) {
        EnableWatchSubscription->Reset();
        EnableWatchSubscription.Reset();
    }
}

void FObserverBase::SetEnabled(bool bInEnabled) {
    bCachedEnabled.store(bInEnabled, std::memory_order_relaxed);
    FConfigRegistry::Get().SetConfig<bool>(EnableConfigKey, bInEnabled);
}

bool FObserverBase::TryPrepareBroadcast() {
    if (!IsEnabled()) {
        return false;
    }
    Version.fetch_add(1, std::memory_order_relaxed);
    return true;
}

FName FObserverBase::BuildEnableConfigKey(const FName& InObserverName) {
    return FName(*FString::Printf(TEXT("Listener.%s.Enabled"), *InObserverName.ToString()));
}

