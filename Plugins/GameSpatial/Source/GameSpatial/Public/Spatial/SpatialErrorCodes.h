/**
 * @file SpatialErrorCodes.h
 * @brief GameSpatial 空间模块强类型错误码与业务常量契约定义
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
     * @brief 非法空间检索参数（如负半径或反向包围盒）
     */
    inline constexpr int32 InvalidQueryParameter = 6003;
}
