/**
 * @file GamePlayerDataRegistry.cpp
 * @brief 玩家动态持久化数据注册中枢实现
 */

#include "Dynamic/GamePlayerDataRegistry.h"
#include "Logging/GameLogCategories.h"

/**
 * @brief 构造函数：使用默认档案值初始化，避免在 CDO 构造期触发日志与动态广播
 */
UGamePlayerDataRegistry::UGamePlayerDataRegistry()
    : CurrentProfile() {
}

/**
 * @brief 获取当前玩家档案（只读引用，随 SetProfile / ResetToDefault 更新）
 * @return 当前档案引用
 */
const FPlayerProfileData& UGamePlayerDataRegistry::GetProfile() const {
    return CurrentProfile;
}

/**
 * @brief 更新当前玩家档案并广播变更事件
 * @param InProfile 新的玩家档案数据
 */
void UGamePlayerDataRegistry::SetProfile(const FPlayerProfileData& InProfile) {
    CurrentProfile = InProfile;
    GAME_LOG(Log, TEXT("【玩家数据中枢】更新玩家档案: PlayerId=%s, Level=%d, Gold=%lld。"),
        *InProfile.PlayerId, InProfile.Progression.Level, InProfile.Progression.Gold);
    OnProfileChanged.Broadcast(CurrentProfile);
}

/**
 * @brief 重置玩家档案为默认初始值并广播变更事件
 */
void UGamePlayerDataRegistry::ResetToDefault() {
    GAME_LOG(Log, TEXT("【玩家数据中枢】重置玩家档案为默认初始值。"));
    SetProfile(FPlayerProfileData());
}
