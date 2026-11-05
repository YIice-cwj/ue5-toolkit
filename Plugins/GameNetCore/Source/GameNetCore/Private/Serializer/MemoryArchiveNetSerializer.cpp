// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serializer/MemoryArchiveNetSerializer.h"

#include "Memory/MemoryView.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

/**
 * @brief 将反射结构体通过二进制属性流高速序列化为字节数组
 * @param StructData 待序列化的结构体实例首地址
 * @param StructType 结构体反射元数据指针
 * @return 写入成功且无归档错误时返回字节数组，失败返回空值
 */
TOptional<TArray<uint8>> FMemoryArchiveNetSerializer::Serialize(
    const void* StructData,
    const UStruct* StructType
) const {
    if (StructData == nullptr || StructType == nullptr) {
        return NullOpt;
    }

    TArray<uint8> OutBytes;
    FMemoryWriter MemoryWriter(OutBytes, true);
    FObjectAndNameAsStringProxyArchive ProxyWriter(MemoryWriter, false);
    ProxyWriter.SetWantBinaryPropertySerialization(true);

    const_cast<UStruct*>(StructType)->SerializeBin(ProxyWriter, const_cast<void*>(StructData));
    if (MemoryWriter.IsError() || ProxyWriter.IsError()) {
        return NullOpt;
    }
    return OutBytes;
}

/**
 * @brief 从二进制切片完整还原目标反射结构体（严格校验截断越界与尾部脏字节）
 * @param InBytes 二进制负载字节视图
 * @param StructType 目标结构体反射元数据指针
 * @param OutStructData 已初始化的目标结构体内存首地址
 * @return 完整精确消费输入字节且无归档错误时返回 true
 */
bool FMemoryArchiveNetSerializer::Deserialize(
    TConstArrayView<uint8> InBytes,
    const UStruct* StructType,
    void* OutStructData
) const {
    if (StructType == nullptr || OutStructData == nullptr) {
        return false;
    }

    FMemoryReaderView MemoryReader(MakeMemoryView(InBytes.GetData(), InBytes.Num()), true);
    const int64 SafeMaxSerializeBytes = FMath::Max<int64>(static_cast<int64>(InBytes.Num()) * 4 + 256, 4096);
    MemoryReader.ArMaxSerializeSize = SafeMaxSerializeBytes;

    FObjectAndNameAsStringProxyArchive ProxyReader(MemoryReader, true);
    ProxyReader.ArMaxSerializeSize = SafeMaxSerializeBytes;
    ProxyReader.SetWantBinaryPropertySerialization(true);

    const_cast<UStruct*>(StructType)->SerializeBin(ProxyReader, OutStructData);
    return !MemoryReader.IsError() && !ProxyReader.IsError() && MemoryReader.AtEnd();
}

