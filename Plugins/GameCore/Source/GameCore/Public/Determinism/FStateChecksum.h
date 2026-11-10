/**
 * @file FStateChecksum.h
 * @brief 64 位确定性状态校验器与联机分叉探测器
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 零堆内存分配的 64 位增量流式状态校验器
 *
 * 专为联机网络同步、帧同步、死亡回放与确定性自动化测试设计：
 * 1. 采用高雪崩位混合算法（Mix64），输入任意 1 bit 偏差均引发输出哈希完全雪崩；
 * 2. 支持整数、定点量化浮点数、三维空间向量与原生字节流的增量注入；
 * 3. 运行全程 0 堆内存申请，单次计算纳秒级，支持每帧高频调用。
 */
class GAMECORE_API FStateChecksumBuilder {
public:
    FStateChecksumBuilder() noexcept;

    /**
     * @brief 注入 32 位无符号整数
     * @param Value 输入数值
     */
    void AddUInt32(uint32 Value) noexcept;

    /**
     * @brief 注入 64 位无符号整数
     * @param Value 输入数值
     */
    void AddUInt64(uint64 Value) noexcept;

    /**
     * @brief 注入按指定精度量化截断的单精度浮点数（消除微小浮点舍入抖动）
     * @param Value 输入浮点数
     * @param Precision 量化精度（默认 0.001f，即毫米/千分位级）
     */
    void AddFloatQuantized(float Value, float Precision = 0.001f) noexcept;

    /**
     * @brief 注入按指定精度量化截断的三维空间向量
     * @param Vec 输入三维向量
     * @param Precision 量化精度（默认 0.01f，即厘米级）
     */
    void AddVectorQuantized(const FVector& Vec, float Precision = 0.01f) noexcept;

    /**
     * @brief 注入原生二进制连续字节流
     * @param Data 字节流首地址
     * @param Size 字节数
     */
    void AddBytes(const void* Data, size_t Size) noexcept;

    /**
     * @brief 计算并输出最终的 64 位确定性状态哈希码
     * @return 最终校验码
     */
    [[nodiscard]] uint64 Finalize() const noexcept;

    /**
     * @brief 重置哈希状态机以供复用
     */
    void Reset() noexcept;

private:
    uint64 HashState;
};

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
