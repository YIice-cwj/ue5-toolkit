/**
 * @file FStaticDataRepository.h
 * @brief 类型擦除泛型静态数据仓库（纯 C++ 脱机可实例化，行负载容器与序列化器注册）
 *
 * 设计思路：仓库对行类型零知识——所有行以二进制负载存储，负载读写由**生成式序列化器**
 * （数据表生成管线的 serialization_generator 产出，经 RegisterRowSerializer 登记）承担，
 * 仓库仅按结构名调度，全程不经 UScriptStruct 反射；不依赖 UWorld/UGameInstance，可脱机实例化。
 * 边界声明：本仓库不携带数据版本（版本仍归 FStaticDataSnapshot），不接入运行时查询路径；
 * `.bin` 文件格式编解码（Magic / 版本 / CRC32 / 摘要）由 `FStaticDataBinCodec` 承担。
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "StaticData/FStaticDataRepositoryTypes.h"
#include "Types/TGameResult.h"

class FMemoryReader;

/**
 * @brief 类型擦除泛型静态数据仓库（.bin 行负载容器，纯 C++ 可脱机实例化）
 */
class GAMEDATACORE_API FStaticDataRepository {
public:
    /**
     * @brief 行负载读写函数指针（生成式免反射；RowPtr 指向行结构体，实现按 Ar.IsSaving() 分流读写）
     */
    using FRowPayloadFunc = bool (*)(FArchive& Ar, void* RowPtr);

public:
    FStaticDataRepository() = default;

    /**
     * @brief 登记某行结构的生成式负载读写器（同结构重复登记即覆盖）
     * @param StructName 行结构名（与 RowType::StaticStruct()->GetName() 严格一致）
     * @param PayloadFunc 行负载读写函数（由数据表生成管线产出）
     */
    void RegisterRowSerializer(const FName& StructName, FRowPayloadFunc PayloadFunc);

    /**
     * @brief 写入一表（同结构重复写入即整体覆盖）
     * @tparam TRowType 行结构体类型（其结构名须已登记负载读写器）
     * @param InRows 待写入行表（RowName -> 行结构体）
     * @return 成功返回 true；未登记读写器或任一行序列化失败返回 false（置错误码，且不改变已装载数据）
     *
     * 实现思路：结构名取自 TRowType::StaticStruct()->GetName()；负载由登记读写器生成，
     * 故 FArchive 的 FString/FName/bool 运算符仅有非 const 重载，此处对行做一次只读的 const 去除。
     */
    template <typename TRowType>
    [[nodiscard]] bool AddTable(const TMap<FName, TRowType>& InRows);

    /**
     * @brief 提取一表（表不存在返回 false；成功时整体覆盖输出表，失败时输出表保持不变）
     * @tparam TRowType 行结构体类型（以 StaticStruct 名匹配仓库内镜像）
     * @param OutRows 输出行表
     * @return 成功返回 true；表不存在、未登记读写器或任一行反序列化失败返回 false（并置错误码）
     */
    template <typename TRowType>
    [[nodiscard]] bool ExtractTable(TMap<FName, TRowType>& OutRows) const;

    /**
     * @brief 写入一表（强类型契约版本）
     * @tparam TRowType 行结构体类型
     * @param InRows 待写入行表
     * @return 成功返回 Success，失败返回 Failure
     */
    template <typename TRowType>
    [[nodiscard]] TGameStatusResult<void> TryAddTable(const TMap<FName, TRowType>& InRows);

    /**
     * @brief 提取一表（强类型契约版本）
     * @tparam TRowType 行结构体类型
     * @return 成功返回包含行表的 Result，失败返回错误枚举
     */
    template <typename TRowType>
    [[nodiscard]] TGameResult<TMap<FName, TRowType>, EStaticDataBinError> TryExtractTable() const;

    /**
     * @brief 查询当前装载表数
     * @return 表数量
     */
    [[nodiscard]] int32 TableCount() const noexcept;

    /**
     * @brief 清空全部装载数据并复位错误码（已登记的负载读写器保留）
     */
    void Reset() noexcept;

    /**
     * @brief 导出全部装载表至 .bin 文件（脱机导出，Cook 前置；字节编码委托 FStaticDataBinCodec）
     * @param FilePath 目标文件绝对路径
     * @return 成功返回 true；编码或写盘失败返回 false（置 IoFailure）
     */
    [[nodiscard]] bool ExportToFile(const FString& FilePath) const;

    /**
     * @brief 自 .bin 文件装载全部表（校验 Magic / 版本 / CRC，失败保持原状）
     * @param FilePath 源文件绝对路径
     * @return 成功返回 true；读取或解码失败返回 false（置对应错误码，已装载数据零触碰）
     */
    [[nodiscard]] bool ImportFromFile(const FString& FilePath);

    /**
     * @brief 获取最近一次失败的错误码
     * @return 错误码（无错误为 None）
     */
    [[nodiscard]] EStaticDataBinError GetLastError() const noexcept;

    /**
     * @brief 计算仓库内容摘要（与装载顺序无关，供双通道数据一致性对拍）
     * @return 表数、总行数与 SHA-1 摘要；空仓库返回 SHA-1 空输入摘要
     *
     * 实现思路：摘要计算委托 FStaticDataBinCodec，表按结构名、行按行键排序后并入字节流
     */
    [[nodiscard]] FStaticDataContentDigest ComputeContentDigest() const;

private:
    /**
     * @brief 行结构体二进制序列化（经登记读写器，无类型知识）
     * @param StructName 行结构名（须已登记读写器）
     * @param RowPtr 行数据指针（读写器只读，不修改行数据）
     * @param OutPayload 输出二进制负载
     * @return 序列化成功返回 true；未登记读写器或归档写出失败返回 false（并置错误码）
     */
    bool SerializeRowToPayload(const FName& StructName, void* RowPtr, TArray<uint8>& OutPayload) const;

    /**
     * @brief 行二进制负载反序列化（越界/损坏经 FMemoryReader::IsError 检出）
     * @param StructName 行结构名（须已登记读写器）
     * @param InPayload 输入二进制负载
     * @param OutRowPtr 输出行数据缓冲
     * @return 反序列化成功返回 true；未登记读写器或负载损坏返回 false（并置错误码）
     */
    bool DeserializeRowFromPayload(const FName& StructName, const TArray<uint8>& InPayload, void* OutRowPtr) const;

    TMap<FName, FStaticDataBinaryTable> Tables;
    TMap<FName, FRowPayloadFunc> RowPayloadFuncs;
    mutable EStaticDataBinError LastError = EStaticDataBinError::None;
};

template <typename TRowType>
bool FStaticDataRepository::AddTable(const TMap<FName, TRowType>& InRows) {
    const FName StructName(TRowType::StaticStruct()->GetName());
    FStaticDataBinaryTable Table;
    Table.StructName = StructName.ToString();
    Table.Rows.Reserve(InRows.Num());
    for (const TPair<FName, TRowType>& Pair : InRows) {
        TArray<uint8>& Payload = Table.Rows.Emplace(Pair.Key);
        if (!SerializeRowToPayload(StructName, const_cast<TRowType*>(&Pair.Value), Payload)) {
            return false;
        }
    }
    Tables.Add(StructName, MoveTemp(Table));
    LastError = EStaticDataBinError::None;
    return true;
}

template <typename TRowType>
bool FStaticDataRepository::ExtractTable(TMap<FName, TRowType>& OutRows) const {
    const FName StructName(TRowType::StaticStruct()->GetName());
    const FStaticDataBinaryTable* Table = Tables.Find(StructName);
    if (Table == nullptr) {
        LastError = EStaticDataBinError::TableNotFound;
        return false;
    }

    TMap<FName, TRowType> Built;
    Built.Reserve(Table->Rows.Num());
    for (const TPair<FName, TArray<uint8>>& Pair : Table->Rows) {
        TRowType Row;
        if (!DeserializeRowFromPayload(StructName, Pair.Value, &Row)) {
            return false;
        }
        Built.Add(Pair.Key, MoveTemp(Row));
    }
    OutRows = MoveTemp(Built);
    LastError = EStaticDataBinError::None;
    return true;
}

template <typename TRowType>
TGameStatusResult<void> FStaticDataRepository::TryAddTable(const TMap<FName, TRowType>& InRows) {
    if (AddTable(InRows)) {
        return TGameStatusResult<void>::Success();
    }
    return TGameStatusResult<void>::Failure(static_cast<int32>(GetLastError()), TEXT("Failed to add table"));
}

template <typename TRowType>
TGameResult<TMap<FName, TRowType>, EStaticDataBinError> FStaticDataRepository::TryExtractTable() const {
    TMap<FName, TRowType> OutRows;
    if (ExtractTable(OutRows)) {
        return TGameResult<TMap<FName, TRowType>, EStaticDataBinError>::Success(MoveTemp(OutRows));
    }
    return TGameResult<TMap<FName, TRowType>, EStaticDataBinError>::Failure(GetLastError());
}
