/**
 * @file DataTableLoaderUtils.h
 * @brief 通用数据表 JSON 反序列化工具类定义（模板实现定义于本头文件类外）
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "Types/TGameResult.h"

#include "Logging/GameLogCategories.h"

/**
 * @brief 通用数据表 JSON 反序列化工具类（UE5 命名，纯静态模板方法）
 */
class FDataTableLoaderUtils {
public:
    FDataTableLoaderUtils() = delete;

    /**
     * @brief 从指定 JSON 文件反序列化加载数据至 TMap 映射表中
     * @tparam TRowType 数据行结构体类型（必须为 USTRUCT）
     * @param FilePath JSON 文件的绝对路径
     * @return 成功返回映射表，失败返回包含错误原因的 StatusResult
     */
    template <typename TRowType>
    [[nodiscard]] static TGameStatusResult<TMap<FName, TRowType>> LoadTableFromJson(const FString& FilePath);
};

template <typename TRowType>
TGameStatusResult<TMap<FName, TRowType>> FDataTableLoaderUtils::LoadTableFromJson(const FString& FilePath) {
    if (!FPaths::FileExists(FilePath)) {
        GAME_LOG(Warning, TEXT("【数据表加载】JSON 文件不存在: %s"), *FilePath);
        return TGameStatusResult<TMap<FName, TRowType>>::Failure(1, FString::Printf(TEXT("文件未找到: %s"), *FilePath));
    }

    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *FilePath)) {
        GAME_LOG(Warning, TEXT("【数据表加载】无法读取文件内容: %s"), *FilePath);
        return TGameStatusResult<TMap<FName, TRowType>>::Failure(2, FString::Printf(TEXT("读取文件失败: %s"), *FilePath));
    }

    TArray<TSharedPtr<FJsonValue>> JsonParsed;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, JsonParsed)) {
        GAME_LOG(Warning, TEXT("【数据表加载】JSON 格式解析失败: %s"), *FilePath);
        return TGameStatusResult<TMap<FName, TRowType>>::Failure(3, FString::Printf(TEXT("JSON 格式解析失败: %s"), *FilePath));
    }

    TMap<FName, TRowType> OutMap;
    OutMap.Reserve(JsonParsed.Num());
    int32 LoadedCount = 0;
    int32 FailedCount = 0;
    int32 RowIndex = 0;

    for (const auto& JsonValue : JsonParsed) {
        ++RowIndex;
        if (!JsonValue.IsValid() || JsonValue->Type != EJson::Object) {
            ++FailedCount;
            GAME_LOG(Error, TEXT("【数据表加载错误】%s 第 %d 条记录非有效 JSON 对象！"),
                *FPaths::GetCleanFilename(FilePath), RowIndex);
            continue;
        }
        const TSharedPtr<FJsonObject> JsonObject = JsonValue->AsObject();

        FString RowNameStr;
        if (!JsonObject->TryGetStringField(TEXT("RowName"), RowNameStr) || RowNameStr.IsEmpty()) {
            if (!JsonObject->TryGetStringField(TEXT("Name"), RowNameStr) || RowNameStr.IsEmpty()) {
                ++FailedCount;
                GAME_LOG(Error, TEXT("【数据表加载错误】%s 第 %d 条记录缺少有效的 RowName/Name 主键字段！"),
                    *FPaths::GetCleanFilename(FilePath), RowIndex);
                continue;
            }
        }
        const FName RowKey(*RowNameStr);
        if (OutMap.Contains(RowKey)) {
            ++FailedCount;
            GAME_LOG(Error, TEXT("【数据表加载错误】%s 第 %d 条记录主键 [%s] 重复出现！"),
                *FPaths::GetCleanFilename(FilePath), RowIndex, *RowNameStr);
            continue;
        }

        TRowType RowData;
        FText FailReason;
        if (FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), TRowType::StaticStruct(), &RowData, 0, 0, false, &FailReason)) {
            OutMap.Add(RowKey, MoveTemp(RowData));
            ++LoadedCount;
        } else {
            ++FailedCount;
            const FString ReasonStr = FailReason.IsEmpty() ? TEXT("字段类型或拼写不匹配") : FailReason.ToString();
            GAME_LOG(Error, TEXT("【数据表加载错误】%s 第 %d 条记录 [%s] 反序列化失败！原因: %s。"),
                *FPaths::GetCleanFilename(FilePath), RowIndex, *RowNameStr, *ReasonStr);
        }
    }

    if (FailedCount > 0) {
        GAME_LOG(Error, TEXT("【数据表加载】%s 加载存在异常！成功: %d, 失败: %d。"),
            *FPaths::GetCleanFilename(FilePath), LoadedCount, FailedCount);
        return TGameStatusResult<TMap<FName, TRowType>>::Failure(4,
            FString::Printf(TEXT("%s 加载失败: 共 %d 行中有 %d 处错误"), *FPaths::GetCleanFilename(FilePath), RowIndex, FailedCount));
    }

    if (LoadedCount == 0) {
        GAME_LOG(Warning, TEXT("【数据表加载】%s 为合法空表（0 条记录），视为加载成功。"),
            *FPaths::GetCleanFilename(FilePath));
        return TGameStatusResult<TMap<FName, TRowType>>::Success(MoveTemp(OutMap));
    }

    GAME_LOG(Log, TEXT("【数据表加载】成功从 %s 加载全部 %d 条数据"), *FPaths::GetCleanFilename(FilePath), LoadedCount);
    return TGameStatusResult<TMap<FName, TRowType>>::Success(MoveTemp(OutMap));
}
