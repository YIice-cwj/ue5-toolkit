/**
 * @file MemoryArchiveNetSerializer.h
 * @brief 基于 UE 原生二进制 Archive 与 SerializeBin 的高速网络序列化适配器
 */

#pragma once

#include "CoreMinimal.h"
#include "Serializer/INetSerializer.h"

/**
 * @brief UE 原生紧凑二进制 Archive 序列化适配器（适用于单机回环及同构 UE 端点高速通信）
 */
class GAMENETCORE_API FMemoryArchiveNetSerializer final : public INetSerializer {
public:
    FMemoryArchiveNetSerializer() = default;
    ~FMemoryArchiveNetSerializer() override = default;

    /**
     * @brief 将反射结构体通过二进制属性流高速序列化为字节数组
     * @param StructData 待序列化的结构体实例首地址
     * @param StructType 结构体反射元数据指针
     * @return 写入成功且无归档错误时返回包含字节数组的成功态，失败返回对应错误
     */
    [[nodiscard]] TGameStatusResult<TArray<uint8>> Serialize(
        const void* StructData,
        const UStruct* StructType
    ) const override;

    /**
     * @brief 从二进制切片完整还原目标反射结构体（严格校验截断越界与尾部脏字节）
     * @param InBytes 二进制负载字节视图
     * @param StructType 目标结构体反射元数据指针
     * @param OutStructData 已初始化的目标结构体内存首地址
     * @return 完整精确消费输入字节且无归档错误时返回成功态，否则返回对应错误
     */
    TGameStatusResult<void> Deserialize(TConstArrayView<uint8> InBytes, const UStruct* StructType, void* OutStructData) const override;
};

