/**
 * @file INetSerializer.h
 * @brief 网络负载序列化与反序列化可插拔抽象契约定义
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/TGameResult.h"
#include "UObject/Class.h"

/**
 * @brief 网络负载序列化适配器抽象基类（支持 JSON、UE 二进制 Archive 及外部 Protobuf 热插拔）
 */
class GAMENETCORE_API INetSerializer {
public:
    virtual ~INetSerializer() = default;

    /**
     * @brief 将反射结构体编码并就地写入外部提供的复用字节缓冲区（避免重复堆分配，子类唯一序列化虚接口）
     * @param StructData 待序列化的结构体实例内存首地址
     * @param StructType 对应的 UStruct 反射元数据指针
     * @param OutBytes 目标写入字节数组（自动重置并写入；失败时保持清空）
     * @return 编码成功返回成功态，失败返回携带 Game::ModuleId::Net 的对应错误
     */
    [[nodiscard]] virtual TGameStatusResult<void> SerializeToBuffer(
        const void* StructData,
        const UStruct* StructType,
        TArray<uint8>& OutBytes
    ) const = 0;

    /**
     * @brief 将接收到的二进制字节切片解码填充至目标反射结构体实例
     * @param InBytes 接收到的负载二进制视图
     * @param StructType 目标 UStruct 反射元数据指针
     * @param OutStructData 已构造的目标结构体实例内存首地址
     * @return 解码成功且数据合法返回成功态，格式畸形或类型不匹配返回对应失败态
     */
    [[nodiscard]] virtual TGameStatusResult<void> Deserialize(
        TConstArrayView<uint8> InBytes,
        const UStruct* StructType,
        void* OutStructData
    ) const = 0;

    /**
     * @brief 便捷封装：将反射结构体实例编码为新建二进制字节数组
     * @param StructData 待序列化的结构体实例内存首地址
     * @param StructType 对应的 UStruct 反射元数据指针
     * @return 编码成功返回包含字节数组的成功态，失败返回对应错误
     */
    [[nodiscard]] TGameStatusResult<TArray<uint8>> Serialize(const void* StructData, const UStruct* StructType) const {
        TArray<uint8> OutBytes;
        const TGameStatusResult<void> Result = SerializeToBuffer(StructData, StructType, OutBytes);
        if (Result.IsFailure()) {
            return TGameStatusResult<TArray<uint8>>::Failure(Result.GetError());
        }
        return TGameStatusResult<TArray<uint8>>::Success(MoveTemp(OutBytes));
    }

    /**
     * @brief 泛型便捷封装：将强类型 USTRUCT 实例编码为字节数组
     * @tparam TStruct 具备反射元数据的结构体类型
     * @param Data 待编码的结构体常引用
     * @return 编码成功返回包含字节数组的成功态，失败返回对应错误
     */
    template <typename TStruct>
    [[nodiscard]] TGameStatusResult<TArray<uint8>> SerializeStruct(const TStruct& Data) const;

    /**
     * @brief 泛型便捷封装：将强类型 USTRUCT 实例编码并就地写入外部复用缓冲区
     * @tparam TStruct 具备反射元数据的结构体类型
     * @param Data 待编码的结构体常引用
     * @param OutBytes 目标写入字节数组
     * @return 编码成功返回成功态，失败返回对应错误
     */
    template <typename TStruct>
    [[nodiscard]] TGameStatusResult<void> SerializeStructTo(const TStruct& Data, TArray<uint8>& OutBytes) const;

    /**
     * @brief 泛型便捷封装：将字节切片反序列化为强类型 USTRUCT 实例
     * @tparam TStruct 具备反射元数据的默认可构造结构体类型
     * @param InBytes 接收到的负载二进制视图
     * @return 解码成功返回包含结构体实例的成功态，失败返回对应错误
     */
    template <typename TStruct>
    [[nodiscard]] TGameStatusResult<TStruct> DeserializeStruct(TConstArrayView<uint8> InBytes) const;

    /**
     * @brief 泛型便捷封装：将字节切片直接反序列化填充至外部已构造的目标结构体实例（零对象二次拷贝）
     * @tparam TStruct 具备反射元数据的结构体类型
     * @param InBytes 接收到的负载二进制视图
     * @param OutData 目标结构体实例引用
     * @return 解码成功且数据合法返回成功态，失败返回对应错误
     */
    template <typename TStruct>
    [[nodiscard]] TGameStatusResult<void> DeserializeStructTo(TConstArrayView<uint8> InBytes, TStruct& OutData) const;
};

template <typename TStruct>
TGameStatusResult<TArray<uint8>> INetSerializer::SerializeStruct(const TStruct& Data) const {
    return Serialize(&Data, TBaseStructure<TStruct>::Get());
}

template <typename TStruct>
TGameStatusResult<void> INetSerializer::SerializeStructTo(const TStruct& Data, TArray<uint8>& OutBytes) const {
    return SerializeToBuffer(&Data, TBaseStructure<TStruct>::Get(), OutBytes);
}

template <typename TStruct>
TGameStatusResult<TStruct> INetSerializer::DeserializeStruct(TConstArrayView<uint8> InBytes) const {
    TStruct DecodedValue{};
    const TGameStatusResult<void> Result = Deserialize(InBytes, TBaseStructure<TStruct>::Get(), &DecodedValue);
    if (Result.IsFailure()) {
        return TGameStatusResult<TStruct>::Failure(Result.GetError());
    }
    return TGameStatusResult<TStruct>::Success(MoveTemp(DecodedValue));
}

template <typename TStruct>
TGameStatusResult<void> INetSerializer::DeserializeStructTo(TConstArrayView<uint8> InBytes, TStruct& OutData) const {
    return Deserialize(InBytes, TBaseStructure<TStruct>::Get(), &OutData);
}
