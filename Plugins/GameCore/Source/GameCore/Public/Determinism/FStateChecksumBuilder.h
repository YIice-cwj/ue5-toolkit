// Copyright Epic Games, Inc. All Rights Reserved.

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
     * @brief 注入布尔值
     * @param bValue 输入布尔值
     */
    void AddBool(bool bValue) noexcept;

    /**
     * @brief 注入 32 位有符号整数
     * @param Value 输入数值
     */
    void AddInt32(int32 Value) noexcept;

    /**
     * @brief 注入 64 位有符号整数
     * @param Value 输入数值
     */
    void AddInt64(int64 Value) noexcept;

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
     * @brief 注入字符串视图（按字符底层字节连续注入，各端表现严格一致）
     * @param Str 输入字符串视图
     */
    void AddString(FStringView Str) noexcept;

    /**
     * @brief 注入 FName（自动转为全字符串内容注入，杜绝各端资产加载顺序不同导致的索引伪分叉）
     * @param Name 输入 FName
     */
    void AddName(const FName& Name) noexcept;

    /**
     * @brief 注入按指定精度量化截断的单精度浮点数（消除微小浮点舍入抖动）
     * @param Value 输入浮点数
     * @param Precision 量化精度（默认 0.001f，即毫米/千分位级）
     */
    void AddFloatQuantized(float Value, float Precision = 0.001f) noexcept;

    /**
     * @brief 注入按指定精度量化截断的双精度浮点数（保留大世界坐标高精度，消除微小浮点舍入抖动）
     * @param Value 输入双精度浮点数
     * @param Precision 量化精度（默认 0.001，即毫米/千分位级）
     */
    void AddDoubleQuantized(double Value, double Precision = 0.001) noexcept;

    /**
     * @brief 注入按指定精度量化截断的三维空间向量（支持 UE5 LWC 大世界双精度坐标）
     * @param Vec 输入三维向量
     * @param Precision 量化精度（默认 0.01，即厘米级）
     */
    void AddVectorQuantized(const FVector& Vec, double Precision = 0.01) noexcept;

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
