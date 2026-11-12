// Copyright Epic Games, Inc. All Rights Reserved.

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

/**
 * @brief 双端联机与回放状态分叉探测器
 */
class GAMECORE_API FDesyncDetector {
public:
    DECLARE_DELEGATE_OneParam(FOnDesyncDetected, const FDesyncReport&);

    static constexpr uint32 HistoryCapacity = 128;
    static_assert((HistoryCapacity & (HistoryCapacity - 1)) == 0, "HistoryCapacity must be power of two");

private:
    struct FHistorySlot {
        uint64 Frame = 0ULL;
        uint64 Checksum = 0ULL;
        bool bValid = false;
    };

public:
    FDesyncDetector() = default;
    ~FDesyncDetector() = default;

    FDesyncDetector(const FDesyncDetector&) = delete;
    FDesyncDetector& operator=(const FDesyncDetector&) = delete;
    FDesyncDetector(FDesyncDetector&&) noexcept = default;
    FDesyncDetector& operator=(FDesyncDetector&&) noexcept = default;

    /**
     * @brief 记录本地指定帧计算出的状态校验码
     * @param Frame 仿真帧序号
     * @param Checksum 本地校验码
     */
    void RecordLocalChecksum(uint64 Frame, uint64 Checksum) noexcept;

    /**
     * @brief 校验远端传入的帧校验码
     * @param Frame 远端帧序号
     * @param RemoteChecksum 远端校验码
     * @param OutReport 校验报告输出
     * @return 存在本地对应帧且未发生分叉返回 true，发生分叉或未找到返回 false
     */
    bool VerifyRemoteChecksum(uint64 Frame, uint64 RemoteChecksum, FDesyncReport& OutReport);

    /**
     * @brief 绑定状态分叉报警回调
     * @param InCallback 回调委托
     */
    void SetDesyncCallback(FOnDesyncDetected InCallback);

    /**
     * @brief 获取探测器历史记录槽位容量
     * @return 静态容量上限
     */
    [[nodiscard]] constexpr uint32 GetCapacity() const noexcept {
        return HistoryCapacity;
    }

    /**
     * @brief 检查本地历史记录中是否存在指定帧的记录
     * @param Frame 目标帧序号
     * @return 存在有效记录返回 true
     */
    [[nodiscard]] bool HasFrame(uint64 Frame) const noexcept;

    /**
     * @brief 查询本地历史记录中是否存在指定帧的记录
     * @param Frame 目标帧序号
     * @param OutChecksum 输出校验码
     * @return 存在有效记录返回 true
     */
    [[nodiscard]] bool GetLocalChecksum(uint64 Frame, uint64& OutChecksum) const noexcept;

    /**
     * @brief 清空所有历史快照记录
     */
    void Reset() noexcept;

private:
    static constexpr uint32 Mask = HistoryCapacity - 1;

    FHistorySlot History[HistoryCapacity];
    FOnDesyncDetected OnDesyncDetectedDelegate;
};
