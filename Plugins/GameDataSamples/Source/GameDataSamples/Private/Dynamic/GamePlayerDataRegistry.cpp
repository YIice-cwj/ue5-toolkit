/**
 * @file GamePlayerDataRegistry.cpp
 * @brief 玩家动态持久化数据注册中枢实现
 */

#include "Dynamic/GamePlayerDataRegistry.h"

UGamePlayerDataRegistry::UGamePlayerDataRegistry() {
    ResetToDefault();
}

void UGamePlayerDataRegistry::SetProfile(const FPlayerProfileData& InProfile) {
    CurrentProfile = InProfile;
    OnProfileChanged.Broadcast(CurrentProfile);
}

void UGamePlayerDataRegistry::ResetToDefault() {
    SetProfile(FPlayerProfileData());
}
