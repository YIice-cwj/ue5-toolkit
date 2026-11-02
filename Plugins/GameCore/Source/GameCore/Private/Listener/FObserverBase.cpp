/**
 * @file FObserverBase.cpp
 * @brief 统一监听基类实现：以配置中心作为启停开关唯一真源
 */

#include "Listener/FObserverBase.h"

FObserverBase::FObserverBase(const FName& InObserverName)
    : ObserverName(InObserverName),
      EnableConfigKey(BuildEnableConfigKey(InObserverName)) {
    FConfigRegistry::Get().RegisterConfig<bool>(EnableConfigKey, true);
}

FObserverBase::~FObserverBase() = default;

void FObserverBase::SetEnabled(bool bInEnabled) {
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

