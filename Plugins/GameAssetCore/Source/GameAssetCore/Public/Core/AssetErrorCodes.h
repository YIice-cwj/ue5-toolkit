/**
 * @file AssetErrorCodes.h
 * @brief GameAssetCore 资产模块强类型错误码常量契约定义（配合 Game::ModuleId::Asset 使用）
 */

#pragma once

#include "CoreMinimal.h"

namespace Game::AssetErrorCodes {

/**
 * @brief 目标资产软引用或软路径为空、未配置或格式非法
 */
inline constexpr int32 InvalidAssetPath = 3001;

/**
 * @brief 底层同步或异步流式加载失败（磁盘包体不存在或流式句柄报错）
 */
inline constexpr int32 LoadFailed = 3002;

/**
 * @brief 已解析资产或类对象与目标强类型契约（Cast / IsChildOf）不兼容
 */
inline constexpr int32 TypeMismatch = 3003;

/**
 * @brief 目标资产路径当前未处于常驻保活锁定状态，无法执行解除锁定
 */
inline constexpr int32 AssetNotPinned = 3004;

/**
 * @brief 在非 GameThread 工作线程对尚未驻留内存的资产请求同步磁盘加载
 */
inline constexpr int32 InvalidThread = 3005;

}  // namespace Game::AssetErrorCodes
