/**
 * @file PoolErrorCodes.h
 * @brief GamePool 对象池模块强类型错误码常量契约定义（配合 Game::ModuleId::Pool 使用）
 */

#pragma once

#include "CoreMinimal.h"

namespace Game::PoolErrorCodes {

/**
 * @brief 目标类型无效（为空、抽象类或类型约束不匹配，如向纯 UObject 池传入 AActor 派生类）
 */
inline constexpr int32 InvalidClass = 5001;

/**
 * @brief 目标类型与强类型模板参数 T 不兼容（非 T 的子类）
 */
inline constexpr int32 TypeMismatch = 5002;

/**
 * @brief 所属运行上下文无效（World 或 Outer 尚未初始化或已销毁）
 */
inline constexpr int32 InvalidContext = 5003;

/**
 * @brief 底层对象实例化失败（SpawnActor 或 NewObject 返回空指针）
 */
inline constexpr int32 SpawnFailed = 5004;

/**
 * @brief 对象在 OnAcquireFromPool / OnAcquireFromPoolWithPayload 回调期间发生自归还或自销毁导致借出失效
 */
inline constexpr int32 InvalidatedDuringAcquire = 5005;

/**
 * @brief 待归还实例为空、已销毁或已标记垃圾回收
 */
inline constexpr int32 InvalidInstance = 5006;

/**
 * @brief 待归还实例不属于当前对象池的活跃借出成员（拒绝外部对象或重复归还）
 */
inline constexpr int32 NotOwnedActiveInstance = 5007;

/**
 * @brief 受管安全句柄无效、世代已过期或归属其他子系统实例
 */
inline constexpr int32 InvalidHandle = 5008;

/**
 * @brief 空闲对象池已达 MaxIdleCount 容量上限，拒绝继续预热填充
 */
inline constexpr int32 CapacityLimitReached = 5009;

/**
 * @brief 跨线程无锁延迟归还队列已满或尚未初始化，无法继续入队
 */
inline constexpr int32 DeferredQueueFull = 5010;

}  // namespace Game::PoolErrorCodes
