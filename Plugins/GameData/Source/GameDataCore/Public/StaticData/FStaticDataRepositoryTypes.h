/**
 * @file FStaticDataRepositoryTypes.h
 * @brief 静态数据仓库类型契约与错误码定义（单一职责 · 错误码与内容摘要）
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 静态数据二进制档错误码（Load/Extract 失败诊断，忽略返回值不构成缺陷）
 */
enum class EStaticDataBinError : uint8 {
    /**
     * @brief 无错误，操作成功
     */
    None,

    /**
     * @brief 底层文件不存在 / 读取失败 / 数据结构截断 / 负载写出失败
     */
    IoFailure,

    /**
     * @brief 二进制文件头 Magic 魔数不匹配
     */
    MagicMismatch,

    /**
     * @brief 二进制格式版本高于当前运行库所支持的版本
     */
    UnsupportedVersion,

    /**
     * @brief 负载整体 CRC32 校验和不匹配（数据被破坏）
     */
    ChecksumMismatch,

    /**
     * @brief 按指定结构体名称在归档中未查找到对应的数据表
     */
    TableNotFound,

    /**
     * @brief 单行负载反序列化失败（行内容已损坏）
     */
    RowDeserializeFailed,

    /**
     * @brief 对应结构体名称尚未注册生成式负载读写器函数
     */
    SerializerNotFound,
};

/**
 * @brief 静态数据内容摘要（双通道对拍用：表数 / 总行数 / 顺序无关摘要）
 */
struct FStaticDataContentDigest {
    int32 TableCount = 0;
    int32 RowCount = 0;
    FString Digest;
};

