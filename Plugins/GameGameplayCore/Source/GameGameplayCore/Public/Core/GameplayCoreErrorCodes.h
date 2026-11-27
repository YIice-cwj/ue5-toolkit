/**
 * @file GameplayCoreErrorCodes.h
 * @brief GameGameplayCore 战斗核心模块强类型错误码常量契约定义（配合 Game::ModuleId::Gameplay 使用）
 */

#pragma once

#include "CoreMinimal.h"

namespace Game::GameplayCoreErrorCodes {

/**
 * @brief 非法参数（无效 GameplayTag、非有限浮点 NaN/Inf 或非正数层数）
 */
inline constexpr int32 InvalidParameter = 11001;

/**
 * @brief 非网络权威端且未开启非权威写入许可，拒绝修改权威状态
 */
inline constexpr int32 NotAuthority = 11002;

/**
 * @brief 目标属性标签尚未在数值组件中注册
 */
inline constexpr int32 StatNotFound = 11003;

/**
 * @brief 目标属性修正器句柄无效、代际已过期或不存在
 */
inline constexpr int32 ModifierHandleNotFound = 11004;

/**
 * @brief 目标 Buff 身份标签或运行实例 ID 不存在
 */
inline constexpr int32 BuffNotFound = 11005;

/**
 * @brief 独立实例策略下该 Buff 标签已达到最大叠层上限
 */
inline constexpr int32 MaxBuffStacksReached = 11006;

}  // namespace Game::GameplayCoreErrorCodes
