/**
 * @file INetSerializer.h
 * @brief 网络负载序列化与反序列化可插拔抽象契约定义
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"

/**
 * @brief 网络负载序列化适配器抽象基类（支持 JSON、UE 二进制 Archive 及外部 Protobuf 热插拔）
 */
class GAMENETCORE_API INetSerializer {
public:
    virtual ~INetSerializer() = default;

    /**
     * @brief 将反射结构体实例编码为二进制字节数组
     * @param StructData 待序列化的结构体实例内存首地址
     * @param StructType 对应的 UStruct 反射元数据指针
     * @return 编码成功返回字节数组，元数据为空或编码失败返回空值
     */
    [[nodiscard]] virtual TOptional<TArray<uint8>> Serialize(const void* StructData, const UStruct* StructType) const = 0;

    /**
     * @brief 将接收到的二进制字节切片解码填充至目标反射结构体实例
     * @param InBytes 接收到的负载二进制视图
     * @param StructType 目标 UStruct 反射元数据指针
     * @param OutStructData 已构造的目标结构体实例内存首地址
     * @return 解码成功且数据合法返回 true，格式畸形或类型不匹配返回 false
     */
    virtual bool Deserialize(TConstArrayView<uint8> InBytes, const UStruct* StructType, void* OutStructData) const = 0;

    /**
     * @brief 泛型便捷封装：将强类型 USTRUCT 实例编码为字节数组
     * @tparam TStruct 具备反射元数据的结构体类型
     * @param Data 待编码的结构体常引用
     * @return 编码成功返回字节数组，失败返回空值
     */
    template <typename TStruct>
    [[nodiscard]] TOptional<TArray<uint8>> SerializeStruct(const TStruct& Data) const;

    /**
     * @brief 泛型便捷封装：将字节切片反序列化为强类型 USTRUCT 实例
     * @tparam TStruct 具备反射元数据的默认可构造结构体类型
     * @param InBytes 接收到的负载二进制视图
     * @return 解码成功返回构造好的结构体实例，失败返回空值
     */
    template <typename TStruct>
    [[nodiscard]] TOptional<TStruct> DeserializeStruct(TConstArrayView<uint8> InBytes) const;
};

template <typename TStruct>
TOptional<TArray<uint8>> INetSerializer::SerializeStruct(const TStruct& Data) const {
    return Serialize(&Data, TBaseStructure<TStruct>::Get());
}

template <typename TStruct>
TOptional<TStruct> INetSerializer::DeserializeStruct(TConstArrayView<uint8> InBytes) const {
    TStruct DecodedValue{};
    if (!Deserialize(InBytes, TBaseStructure<TStruct>::Get(), &DecodedValue)) {
        return NullOpt;
    }
    return DecodedValue;
}

