// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serializer/JsonNetSerializer.h"

#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Logging/GameLogCategories.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Types/NetErrorCodes.h"

/**
 * @brief 将反射结构体编码为无缩进紧凑 UTF-8 JSON 字节流并写入外部复用字节数组
 * @param StructData 待序列化的结构体实例首地址
 * @param StructType 结构体反射元数据指针
 * @param OutBytes 目标写入字节数组（复用已有容量，避免重复堆分配）
 * @return 编码成功返回成功态，失败返回对应错误
 */
TGameStatusResult<void> FJsonNetSerializer::SerializeToBuffer(
    const void* StructData,
    const UStruct* StructType,
    TArray<uint8>& OutBytes
) const {
    if (StructData == nullptr || StructType == nullptr) {
        OutBytes.Reset();
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::SerializationFailed,
            TEXT("StructData or StructType is null")
        );
    }

    FString JsonText;
    const bool bExported = FJsonObjectConverter::UStructToFormattedJsonObjectString<TCHAR, TCondensedJsonPrintPolicy>(
        StructType,
        StructData,
        JsonText
    );
    if (!bExported) {
        OutBytes.Reset();
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("[JsonSerializer] UStruct 转 JSON 字符串失败: %s"), *StructType->GetName());
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::SerializationFailed,
            TEXT("Failed to convert UStruct to JSON string")
        );
    }

    const FTCHARToUTF8 Utf8Converter(*JsonText, JsonText.Len());
    OutBytes.Reset();
    OutBytes.Append(reinterpret_cast<const uint8*>(Utf8Converter.Get()), Utf8Converter.Length());
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 将 UTF-8 JSON 字节切片解码写入目标反射结构体
 * @param InBytes UTF-8 JSON 字节切片
 * @param StructType 目标结构体反射元数据指针
 * @param OutStructData 已初始化的目标结构体内存首地址
 * @return JSON 语法合法且字段转换成功返回成功态，否则返回对应错误
 */
TGameStatusResult<void> FJsonNetSerializer::Deserialize(
    TConstArrayView<uint8> InBytes,
    const UStruct* StructType,
    void* OutStructData
) const {
    if (StructType == nullptr || OutStructData == nullptr) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::SerializationFailed,
            TEXT("StructType or OutStructData is null")
        );
    }

    if (InBytes.IsEmpty()) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::SerializationFailed,
            TEXT("Payload bytes view is empty")
        );
    }

    const FUTF8ToTCHAR WideConverter(reinterpret_cast<const ANSICHAR*>(InBytes.GetData()), InBytes.Num());
    const FStringView JsonView(WideConverter.Get(), WideConverter.Length());

    TSharedPtr<FJsonObject> JsonObject;
    const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::CreateFromView(JsonView);
    if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid()) {
        GAME_LOG_THROTTLE(
            1.0,
            Warning,
            TEXT("[JsonSerializer] 非法 JSON 负载语法，反序列化失败: Target=%s, Bytes=%d"),
            *StructType->GetName(),
            InBytes.Num()
        );
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::SerializationFailed,
            TEXT("Malformed JSON payload syntax")
        );
    }

    const bool bConverted = FJsonObjectConverter::JsonObjectToUStruct(
        JsonObject.ToSharedRef(),
        StructType,
        OutStructData,
        0,
        0,
        bStrictMode
    );
    if (!bConverted) {
        GAME_LOG_THROTTLE(
            1.0,
            Warning,
            TEXT("[JsonSerializer] JSON 字段映射至 UStruct 失败: Target=%s, StrictMode=%d"),
            *StructType->GetName(),
            bStrictMode ? 1 : 0
        );
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Net,
            Game::NetErrorCodes::SerializationFailed,
            TEXT("Failed to map JSON fields to UStruct properties")
        );
    }

    return TGameStatusResult<void>::Success();
}
