/**
 * @file FStaticDataBinCodec.h
 * @brief .bin 文件格式编解码器（纯静态 · 单缓冲直写 · Magic / 严格版本 / CRC32 校验与定界顺序无关摘要）
 */

#pragma once

#include "CoreMinimal.h"
#include "StaticData/FStaticDataRepositoryTypes.h"
#include "Types/TGameResult.h"

class FMemoryReader;

/**
 * @brief 静态数据二进制档编解码器（纯静态工具类，禁止实例化）
 */
class GAMEDATACORE_API FStaticDataBinCodec {
public:
    FStaticDataBinCodec() = delete;

    /**
     * @brief 将表集合编码为完整文件字节流（单缓冲直写：Magic + 格式版本 + 表集合 + CRC32）
     * @param InTables 表集合
     * @return 成功返回完整文件字节流，失败返回具体错误码
     */
    [[nodiscard]] static TGameResult<TArray<uint8>, EStaticDataBinError> Encode(const TMap<FName, FStaticDataBinaryTable>& InTables);

    /**
     * @brief 解码完整文件字节流为表集合（严格校验 Magic、精确格式版本、CRC32 与无尾随脏字节）
     * @param InFileBytes 完整文件字节流
     * @return 成功返回解码后的表集合，失败返回具体错误码
     */
    [[nodiscard]] static TGameResult<TMap<FName, FStaticDataBinaryTable>, EStaticDataBinError> Decode(const TArray<uint8>& InFileBytes);

    /**
     * @brief 计算与装载顺序无关且带字段长度定界的内容摘要
     * @param InTables 表集合
     * @return 表数、总行数与摘要；空集合返回 SHA-1 空输入摘要
     *
     * 实现思路：表按结构名、行按行键字典序排序，每段均附带字节长度定界后并入 UTF-8 SHA-1，
     * 与装载顺序解耦且杜绝相邻字段拼接碰撞。
     */
    [[nodiscard]] static FStaticDataContentDigest ComputeDigest(const TMap<FName, FStaticDataBinaryTable>& InTables);

    /**
     * @brief 获取错误码的诊断描述文本
     * @param InError 错误码
     * @return 中文诊断文本
     */
    [[nodiscard]] static FString DescribeError(EStaticDataBinError InError);

    /**
     * @brief 将二进制编解码错误枚举映射为标准 Game::DataErrorCodes 错误码
     * @param InError 编解码错误枚举
     * @return 对应的数据模块整数错误码
     */
    [[nodiscard]] static int32 ToDataErrorCode(EStaticDataBinError InError) noexcept;

    /**
     * @brief 构造归属 Game::ModuleId::Data 的标准化 FGameError 对象
     * @param InError 编解码错误枚举
     * @return 包含模块 ID、数据错误码与诊断文本的 FGameError
     */
    [[nodiscard]] static FGameError MakeError(EStaticDataBinError InError);

private:
    /**
     * @brief 解析负载区为表集合（临时装载，供解码成功时原子换入）
     * @param PayloadReader 已消费 Magic 与版本、定位到表数量字段的读取器
     * @param OutTables 输出表集合
     * @return 解析成功返回 true
     */
    [[nodiscard]] static bool ParseTables(FMemoryReader& PayloadReader, TMap<FName, FStaticDataBinaryTable>& OutTables);

    /**
     * @brief 解析单张表的结构名、行数量与全部行负载
     * @param PayloadReader 定位到单表起始位置的读取器
     * @param OutTable 输出单表二进制镜像
     * @return 解析成功且无重复行键返回 true
     */
    [[nodiscard]] static bool ParseSingleTable(FMemoryReader& PayloadReader, FStaticDataBinaryTable& OutTable);
};
