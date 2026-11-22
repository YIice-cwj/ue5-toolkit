/**
 * @file MessageErrorCodes.h
 * @brief GameMessage 消息总线模块强类型错误码常量契约定义（配合 Game::ModuleId::Message 使用）
 */

#pragma once

#include "CoreMinimal.h"

namespace Game::MessageErrorCodes {

/**
 * @brief 消息通道 GameplayTag 无效或为空
 */
inline constexpr int32 InvalidChannel = 7001;

/**
 * @brief 消息载荷的 UScriptStruct 反射元数据为空
 */
inline constexpr int32 InvalidStructType = 7002;

/**
 * @brief 消息载荷内存指针或任务闭包为空
 */
inline constexpr int32 InvalidPayload = 7003;

/**
 * @brief 消息子系统尚未初始化、正在关闭或已完成注销
 */
inline constexpr int32 SubsystemShuttingDown = 7004;

}  // namespace Game::MessageErrorCodes
