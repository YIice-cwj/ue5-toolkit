/**
 * @file GamePlayerDataRegistry.cpp
 * @brief 玩家动态持久化数据注册中枢实现
 */

#include "Dynamic/GamePlayerDataRegistry.h"
#include "Logging/GameLogCategories.h"

UGamePlayerDataRegistry::UGamePlayerDataRegistry() {
    ResetToDefault();
}

void UGamePlayerDataRegistry::SetProfile(const FPlayerProfileData& InProfile) {
    CurrentProfile = InProfile;
    GAME_LOG(Log, TEXT("【玩家数据中枢】更新玩家档案: PlayerId=%s, Level=%d, Gold=%lld。"),
        *InProfile.PlayerId, InProfile.Progression.Level, InProfile.Progression.Gold);
    OnProfileChanged.Broadcast(CurrentProfile);
}

void UGamePlayerDataRegistry::ResetToDefault() {
    GAME_LOG(Log, TEXT("【玩家数据中枢】重置玩家档案为默认初始值。"));
    SetProfile(FPlayerProfileData());
}
