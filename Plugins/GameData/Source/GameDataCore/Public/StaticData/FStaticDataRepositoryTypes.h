/**
 * @file FStaticDataRepositoryTypes.h
 * @brief 静态数据仓库类型契约与错误码定义（单一职责 · 错误码、表镜像与内容摘要）
 */

#pragma once

#include "CoreMinimal.h"

/**
 * @brief 静态数据二进制档编解码错误码枚举
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
     * @brief 二进制格式版本不受当前运行库支持
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
 * @brief 单表二进制镜像（结构名 + 行键到行负载的映射）
 */
struct FStaticDataBinaryTable {
    /**
     * @brief 行结构体反射名（与 TRowType::StaticStruct()->GetName() 对齐）
     */
    FString StructName;

    /**
     * @brief 行主键到单行二进制序列化负载的映射表
     */
    TMap<FName, TArray<uint8>> Rows;
};

/**
 * @brief 静态数据内容摘要（双通道对拍用：表数 / 总行数 / 顺序无关摘要）
 */
struct FStaticDataContentDigest {
    /**
     * @brief 已装载的数据表总数
     */
    int32 TableCount = 0;

    /**
     * @brief 已装载的全部数据行总数
     */
    int32 RowCount = 0;

    /**
     * @brief 与装载顺序无关的小写十六进制 SHA-1 内容摘要
     */
    FString Digest;
};
