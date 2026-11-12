// Copyright Epic Games, Inc. All Rights Reserved.

#include "Determinism/FDesyncDetector.h"

/**
 * @brief 记录本地指定帧计算出的状态校验码
 * @param Frame 仿真帧序号
 * @param Checksum 本地校验码
 */
void FDesyncDetector::RecordLocalChecksum(uint64 Frame, uint64 Checksum) noexcept {
    const uint32 Slot = static_cast<uint32>(Frame & Mask);
    History[Slot].Frame = Frame;
    History[Slot].Checksum = Checksum;
    History[Slot].bValid = true;
}

/**
 * @brief 校验远端传入的帧校验码
 * @param Frame 远端帧序号
 * @param RemoteChecksum 远端校验码
 * @param OutReport 校验报告输出
 * @return 未分叉且校验成功返回 true
 */
bool FDesyncDetector::VerifyRemoteChecksum(uint64 Frame, uint64 RemoteChecksum, FDesyncReport& OutReport) {
    OutReport.Frame = Frame;
    OutReport.RemoteChecksum = RemoteChecksum;

    const uint32 Slot = static_cast<uint32>(Frame & Mask);
    if (!History[Slot].bValid || History[Slot].Frame != Frame) {
        OutReport.LocalChecksum = 0ULL;
        OutReport.bDiverged = false;
        return false;
    }

    OutReport.LocalChecksum = History[Slot].Checksum;
    if (History[Slot].Checksum == RemoteChecksum) {
        OutReport.bDiverged = false;
        return true;
    }

    OutReport.bDiverged = true;
    if (OnDesyncDetectedDelegate.IsBound()) {
        OnDesyncDetectedDelegate.Execute(OutReport);
    }
    return false;
}

/**
 * @brief 绑定状态分叉报警回调
 * @param InCallback 回调委托
 */
void FDesyncDetector::SetDesyncCallback(FOnDesyncDetected InCallback) {
    OnDesyncDetectedDelegate = MoveTemp(InCallback);
}

/**
 * @brief 检查本地历史记录中是否存在指定帧的记录
 * @param Frame 目标帧序号
 * @return 存在有效记录返回 true
 */
bool FDesyncDetector::HasFrame(uint64 Frame) const noexcept {
    const uint32 Slot = static_cast<uint32>(Frame & Mask);
    return History[Slot].bValid && History[Slot].Frame == Frame;
}

/**
 * @brief 查询本地历史记录中是否存在指定帧的记录
 * @param Frame 目标帧序号
 * @param OutChecksum 输出校验码
 * @return 存在有效记录返回 true
 */
bool FDesyncDetector::GetLocalChecksum(uint64 Frame, uint64& OutChecksum) const noexcept {
    const uint32 Slot = static_cast<uint32>(Frame & Mask);
    if (History[Slot].bValid && History[Slot].Frame == Frame) {
        OutChecksum = History[Slot].Checksum;
        return true;
    }
    return false;
}

/**
 * @brief 清空所有历史快照记录
 */
void FDesyncDetector::Reset() noexcept {
    for (uint32 Index = 0; Index < HistoryCapacity; ++Index) {
        History[Index].bValid = false;
    }
}
