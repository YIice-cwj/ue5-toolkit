// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serializer/JsonNetSerializer.h"

#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

/**
 * @brief 将反射结构体编码为无缩进紧凑 UTF-8 JSON 字节流
 * @param StructData 待序列化的结构体实例首地址
 * @param StructType 结构体反射元数据指针
 * @return 编码成功返回包含 UTF-8 字节数组的成功态，失败返回对应错误
 */
TGameStatusResult<TArray<uint8>> FJsonNetSerializer::Serialize(const void* StructData, const UStruct* StructType) const {
    if (StructData == nullptr || StructType == nullptr) {
        return TGameStatusResult<TArray<uint8>>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2004, TEXT("StructData or StructType is null"))
        );
    }

    FString JsonText;
    const bool bExported = FJsonObjectConverter::UStructToFormattedJsonObjectString<TCHAR, TCondensedJsonPrintPolicy>(
        StructType,
        StructData,
        JsonText
    );
    if (!bExported) {
        return TGameStatusResult<TArray<uint8>>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2004, TEXT("Failed to convert UStruct to JSON string"))
        );
    }

    const FTCHARToUTF8 Utf8Converter(*JsonText, JsonText.Len());
    TArray<uint8> OutBytes;
    OutBytes.Append(reinterpret_cast<const uint8*>(Utf8Converter.Get()), Utf8Converter.Length());
    return TGameStatusResult<TArray<uint8>>::Success(MoveTemp(OutBytes));
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
            FGameError::Make(Game::ModuleId::Net, 2004, TEXT("StructType or OutStructData is null"))
        );
    }

    if (InBytes.IsEmpty()) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2004, TEXT("Payload bytes view is empty"))
        );
    }

    const FUTF8ToTCHAR WideConverter(reinterpret_cast<const ANSICHAR*>(InBytes.GetData()), InBytes.Num());
    const FStringView JsonView(WideConverter.Get(), WideConverter.Length());

    TSharedPtr<FJsonObject> JsonObject;
    const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::CreateFromView(JsonView);
    if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid()) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2004, TEXT("Malformed JSON payload syntax"))
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
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Net, 2004, TEXT("Failed to map JSON fields to UStruct properties"))
        );
    }

    return TGameStatusResult<void>::Success();
}

