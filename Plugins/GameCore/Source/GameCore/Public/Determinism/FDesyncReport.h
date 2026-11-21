/**
 * @file FDesyncReport.h
 * @brief 状态不同步/分叉诊断报告包
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 状态不同步/分叉诊断报告包
 */
struct FDesyncReport {
    uint64 Frame = 0ULL;
    uint64 LocalChecksum = 0ULL;
    uint64 RemoteChecksum = 0ULL;
    bool bDiverged = false;
};
