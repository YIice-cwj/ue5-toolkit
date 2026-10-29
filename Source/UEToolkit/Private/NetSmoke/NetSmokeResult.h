/**
 * @file NetSmokeResult.h
 * @brief 网络链路冒烟测试结果落盘工具
 */

#pragma once

#include "CoreMinimal.h"

namespace NetSmokeResult {
    /**
     * @brief 冒烟结果落盘到 Saved/NetSmoke 目录
     * @param InFileName 结果文件名
     * @param InContent 结果内容
     */
    void Write(const TCHAR* InFileName, const FString& InContent);
}

