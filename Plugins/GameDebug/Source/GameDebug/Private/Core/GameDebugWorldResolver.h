/**
 * @file GameDebugWorldResolver.h
 * @brief GameDebug 模块内部世界上下文安全解析工具（严禁非空无效上下文穿透污染无关世界）
 */

#pragma once

#include "CoreMinimal.h"

class UObject;
class UWorld;

namespace Game::Debug::Private {

/**
 * @brief 根据可选世界上下文解析目标 UWorld 指针
 * @param WorldContextObject 世界上下文对象；传入 nullptr 时按活跃 GameWorld 优先规则兜底，传入非空无效对象时直接返回 nullptr
 * @return 解析得到的有效 UWorld 指针，无法解析时返回 nullptr
 */
[[nodiscard]] UWorld* ResolveWorld(const UObject* WorldContextObject);

}  // namespace Game::Debug::Private
