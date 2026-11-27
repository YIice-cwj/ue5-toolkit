/**
 * @file SpatialErrorCodes.h
 * @brief GameSpatial 空间模块强类型错误码契约定义（配合 Game::ModuleId::Spatial 使用）
 */

#pragma once

#include "CoreMinimal.h"

namespace Game::SpatialErrorCodes {
    /**
     * @brief 空间网格中未检索到目标元素
     */
    inline constexpr int32 ElementNotFound = 6001;

    /**
     * @brief 目标元素已存在于空间网格中，重复插入被拦截
     */
    inline constexpr int32 ElementAlreadyExists = 6002;

    /**
     * @brief 非法空间坐标（包含 NaN 或 Inf 非有限浮点分量）
     */
    inline constexpr int32 InvalidCoordinate = 6003;
}
