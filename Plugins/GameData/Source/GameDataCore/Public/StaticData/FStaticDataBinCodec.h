/**
 * @file FStaticDataBinCodec.h
 * @brief .bin 文件格式编解码器（纯静态 · Magic / 版本 / CRC32 校验与顺序无关摘要）
 */

#pragma once

#include "CoreMinimal.h"
#include "StaticData/FStaticDataRepositoryTypes.h"

class FMemoryReader;

/**
 * @brief 静态数据二进制档编解码器
 */
class GAMEDATACORE_API FStaticDataBinCodec {
public:
    /**
     * @brief 将表集合编码为完整文件字节流（Magic + 格式版本 + 表集合 + CRC32）
     * @param InTables 表集合
     * @param OutFileBytes 输出的完整文件字节流
     * @return 内存写入器未出错返回 true
     */
    [[nodiscard]] static bool Encode(const TMap<FName, FStaticDataBinaryTable>& InTables, TArray<uint8>& OutFileBytes);

    /**
     * @brief 解码完整文件字节流为表集合
     * @param InFileBytes 完整文件字节流
     * @param OutTables 输出的表集合；解码失败时保持不变
     * @return 成功返回 None，否则返回具体错误码
     */
    [[nodiscard]] static EStaticDataBinError Decode(
        const TArray<uint8>& InFileBytes,
        TMap<FName, FStaticDataBinaryTable>& OutTables
    );

    /**
     * @brief 计算与装载顺序无关的内容摘要
     * @param InTables 表集合
     * @return 表数、总行数与摘要；空集合返回 SHA-1 空输入摘要
     *
     * 实现思路：表按结构名、行按行键排序后逐项并入「结构名 + 行数 + 行键 + 行负载」字节流，
     * 与装载顺序解耦——同一批数据无论经 .bin 还是 JSON 通道装载，摘要必须一致
     */
    [[nodiscard]] static FStaticDataContentDigest ComputeDigest(const TMap<FName, FStaticDataBinaryTable>& InTables);

    /**
     * @brief 获取错误码的诊断描述
     * @param InError 错误码
     * @return 中文诊断文本
     */
    [[nodiscard]] static FString DescribeError(EStaticDataBinError InError);

private:
    /**
     * @brief 解析负载区为表集合（临时装载，供解码成功时原子换入）
     * @param PayloadReader 已消费 Magic 与版本、定位到表数量字段的读取器
     * @param OutTables 输出表集合
     * @return 解析成功返回 true
     */
    [[nodiscard]] static bool ParseTables(FMemoryReader& PayloadReader, TMap<FName, FStaticDataBinaryTable>& OutTables);
};
