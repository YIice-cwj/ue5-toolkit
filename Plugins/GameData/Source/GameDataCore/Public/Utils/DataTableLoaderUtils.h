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
     * @param OutMap 输出的目标映射表（RowName -> RowStruct）；失败时会被清空，不残留半填充数据
     * @return 是否成功读取并反序列化出数据
     *
     * 实现思路：读取 JSON 文本，通过 FJsonSerializer 反序列化数组，提取 RowName/Name 作为主键并通过 FJsonObjectConverter 反序列化行结构体
     */
    template <typename TRowType>
    [[nodiscard]] static bool LoadTableFromJson(const FString& FilePath, TMap<FName, TRowType>& OutMap);
};

// ---------------------------------------------------------------------------
// 模板实现（定义全部置于类外，随头文件对使用方可见）
// ---------------------------------------------------------------------------

template <typename TRowType>
bool FDataTableLoaderUtils::LoadTableFromJson(const FString& FilePath, TMap<FName, TRowType>& OutMap) {
    if (!FPaths::FileExists(FilePath)) {
        GAME_LOG(Warning, TEXT("【数据表加载】JSON 文件不存在: %s"), *FilePath);
        return false;
    }

    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *FilePath)) {
        GAME_LOG(Warning, TEXT("【数据表加载】无法读取文件内容: %s"), *FilePath);
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> JsonParsed;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, JsonParsed)) {
        GAME_LOG(Warning, TEXT("【数据表加载】JSON 格式解析失败: %s"), *FilePath);
        return false;
    }

    OutMap.Empty();
    OutMap.Reserve(JsonParsed.Num());
    int32 LoadedCount = 0;
    int32 FailedCount = 0;
    int32 RowIndex = 0;

    for (const auto& JsonValue : JsonParsed) {
        ++RowIndex;
        if (!JsonValue.IsValid()) {
            ++FailedCount;
            GAME_LOG(Error, TEXT("【数据表加载错误】%s 第 %d 条记录为无效 JSON 数据！"),
                *FPaths::GetCleanFilename(FilePath), RowIndex);
            continue;
        }
        const TSharedPtr<FJsonObject> JsonObject = JsonValue->AsObject();
        if (!JsonObject.IsValid()) {
            ++FailedCount;
            GAME_LOG(Error, TEXT("【数据表加载错误】%s 第 %d 条记录非有效 JSON 对象！"),
                *FPaths::GetCleanFilename(FilePath), RowIndex);
            continue;
        }

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
        if (FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), TRowType::StaticStruct(), &RowData)) {
            OutMap.Add(RowKey, RowData);
            ++LoadedCount;
        } else {
            ++FailedCount;
            GAME_LOG(Error, TEXT("【数据表加载错误】%s 第 %d 条记录 [%s] 反序列化失败！请检查字段类型与拼写是否匹配。"),
                *FPaths::GetCleanFilename(FilePath), RowIndex, *RowNameStr);
        }
    }

    if (FailedCount > 0) {
        GAME_LOG(Error, TEXT("【数据表加载】%s 加载存在异常！成功: %d, 失败: %d。"),
            *FPaths::GetCleanFilename(FilePath), LoadedCount, FailedCount);
        OutMap.Empty();
        return false;
    }

    if (LoadedCount == 0) {
        GAME_LOG(Warning, TEXT("【数据表加载】%s 为合法空表（0 条记录），视为加载成功。"),
            *FPaths::GetCleanFilename(FilePath));
        return true;
    }

    GAME_LOG(Log, TEXT("【数据表加载】成功从 %s 加载全部 %d 条数据"), *FPaths::GetCleanFilename(FilePath), LoadedCount);
    return true;
}
