/**
 * @file FStaticDataRepository.h
 * @brief 类型擦除泛型静态数据仓库（纯 C++ 脱机可实例化，行负载容器与序列化器注册）
 *
 * 设计思路：仓库对行类型零知识——所有行以二进制负载存储，负载读写由生成式序列化器
 * （数据表生成管线的 serialization_generator 产出，经 RegisterRowSerializer 登记）承担，
 * 仓库仅按结构名调度，全程不经 UScriptStruct 反射；不依赖 UWorld/UGameInstance，可脱机实例化。
 * 边界声明：本仓库不携带数据版本（版本归 TStaticDataSnapshotStore / FStaticDataSnapshot），
 * 不接入运行时查询路径；.bin 文件格式编解码由 FStaticDataBinCodec 承担。
 */

#pragma once

#include "CoreMinimal.h"
#include "StaticData/FStaticDataBinCodec.h"
#include "StaticData/FStaticDataRepositoryTypes.h"
#include "Types/TGameResult.h"
#include "UObject/Class.h"

/**
 * @brief 类型擦除泛型静态数据仓库（.bin 行负载容器，纯 C++ 可脱机实例化）
 */
class GAMEDATACORE_API FStaticDataRepository {
public:
    /**
     * @brief 行负载读写函数指针（生成式免反射；RowPtr 指向行结构体，实现按 Ar.IsSaving() 分流读写）
     */
    using FRowPayloadFunc = bool (*)(FArchive& Ar, void* RowPtr);

    FStaticDataRepository() = default;
    ~FStaticDataRepository() = default;
    FStaticDataRepository(const FStaticDataRepository&) = default;
    FStaticDataRepository& operator=(const FStaticDataRepository&) = default;
    FStaticDataRepository(FStaticDataRepository&&) noexcept = default;
    FStaticDataRepository& operator=(FStaticDataRepository&&) noexcept = default;

    /**
     * @brief 登记某行结构的生成式负载读写器（同结构重复登记即覆盖，拒绝空名或空函数指针）
     * @param StructName 行结构名（与 TRowType::StaticStruct()->GetName() 严格一致）
     * @param PayloadFunc 行负载读写函数（由数据表生成管线产出）
     */
    void RegisterRowSerializer(const FName& StructName, FRowPayloadFunc PayloadFunc);

    /**
     * @brief 按行结构体类型登记生成式负载读写器（自动取 TRowType::StaticStruct()->GetName() 避免字面量漂移）
     * @tparam TRowType 反射行结构体类型
     * @param PayloadFunc 行负载读写函数
     */
    template <typename TRowType>
    void RegisterRowSerializer(FRowPayloadFunc PayloadFunc);

    /**
     * @brief 写入一表（同结构重复写入即整体覆盖，空表亦校验读写器是否已登记）
     * @tparam TRowType 行结构体类型（其结构名须已登记负载读写器）
     * @param InRows 待写入行表（RowName -> 行结构体）
     * @return 成功返回 Success；未登记读写器或任一行序列化失败返回含 Game::DataErrorCodes 的 Failure
     */
    template <typename TRowType>
    [[nodiscard]] TGameStatusResult<void> AddTable(const TMap<FName, TRowType>& InRows);

    /**
     * @brief 提取一表（空表亦校验读写器是否已登记）
     * @tparam TRowType 行结构体类型（以 StaticStruct 名匹配仓库内镜像）
     * @return 成功返回包含行表的 TGameStatusResult，失败返回含 Game::DataErrorCodes 的 Failure
     */
    template <typename TRowType>
    [[nodiscard]] TGameStatusResult<TMap<FName, TRowType>> ExtractTable() const;

    /**
     * @brief 查询当前装载表数
     * @return 表数量
     */
    [[nodiscard]] int32 TableCount() const noexcept;

    /**
     * @brief 清空全部装载数据（已登记的负载读写器保留）
     */
    void Reset() noexcept;

    /**
     * @brief 导出全部装载表至 .bin 文件（脱机导出，Cook 前置；字节编码委托 FStaticDataBinCodec）
     * @param FilePath 目标文件绝对路径
     * @return 成功返回 Success；编码或写盘失败返回含 Game::DataErrorCodes 的 Failure
     */
    [[nodiscard]] TGameStatusResult<void> ExportToFile(const FString& FilePath) const;

    /**
     * @brief 自 .bin 文件装载全部表（校验 Magic / 版本 / CRC，失败保持原状）
     * @param FilePath 源文件绝对路径
     * @return 成功返回装载表数，失败返回含 Game::DataErrorCodes 的 Failure
     */
    [[nodiscard]] TGameStatusResult<int32> ImportFromFile(const FString& FilePath);

    /**
     * @brief 计算仓库内容摘要（与装载顺序无关，供双通道数据一致性对拍）
     * @return 表数、总行数与 SHA-1 摘要；空仓库返回 SHA-1 空输入摘要
     */
    [[nodiscard]] FStaticDataContentDigest ComputeContentDigest() const;

private:
    /**
     * @brief 按结构体名查找已登记的行负载读写器
     * @param StructName 行结构体名
     * @return 已登记读写器函数指针，未登记返回 nullptr
     */
    [[nodiscard]] FRowPayloadFunc FindRowSerializer(const FName& StructName) const;

    /**
     * @brief 行结构体二进制序列化（使用调用前已解析的读写器，避免热循环重复哈希查找）
     * @param PayloadFunc 非空行负载读写器函数指针
     * @param RowPtr 行数据指针（读写器只读，不修改行数据）
     * @param OutPayload 输出二进制负载
     * @return 序列化成功返回 true，归档写出失败返回 false
     */
    static bool SerializeRowWithFunc(FRowPayloadFunc PayloadFunc, void* RowPtr, TArray<uint8>& OutPayload);

    /**
     * @brief 行二进制负载反序列化（越界/损坏经 FMemoryReader::IsError 检出）
     * @param PayloadFunc 非空行负载读写器函数指针
     * @param InPayload 输入二进制负载
     * @param OutRowPtr 输出行数据缓冲
     * @return 反序列化成功返回 true，负载损坏返回 false
     */
    static bool DeserializeRowWithFunc(FRowPayloadFunc PayloadFunc, const TArray<uint8>& InPayload, void* OutRowPtr);

    TMap<FName, FStaticDataBinaryTable> Tables;
    TMap<FName, FRowPayloadFunc> RowPayloadFuncs;
};

template <typename TRowType>
void FStaticDataRepository::RegisterRowSerializer(FRowPayloadFunc PayloadFunc) {
    RegisterRowSerializer(FName(TRowType::StaticStruct()->GetName()), PayloadFunc);
}

template <typename TRowType>
TGameStatusResult<void> FStaticDataRepository::AddTable(const TMap<FName, TRowType>& InRows) {
    const FName StructName(TRowType::StaticStruct()->GetName());
    const FRowPayloadFunc PayloadFunc = FindRowSerializer(StructName);
    if (PayloadFunc == nullptr) {
        return TGameStatusResult<void>::Failure(FStaticDataBinCodec::MakeError(EStaticDataBinError::SerializerNotFound));
    }

    FStaticDataBinaryTable Table;
    Table.StructName = StructName.ToString();
    Table.Rows.Reserve(InRows.Num());
    for (const TPair<FName, TRowType>& Pair : InRows) {
        if (Pair.Key.IsNone()) {
            return TGameStatusResult<void>::Failure(FStaticDataBinCodec::MakeError(EStaticDataBinError::IoFailure));
        }
        TArray<uint8>& Payload = Table.Rows.Emplace(Pair.Key);
        if (!SerializeRowWithFunc(PayloadFunc, const_cast<TRowType*>(&Pair.Value), Payload)) {
            return TGameStatusResult<void>::Failure(FStaticDataBinCodec::MakeError(EStaticDataBinError::IoFailure));
        }
    }
    Tables.Add(StructName, MoveTemp(Table));
    return TGameStatusResult<void>::Success();
}

template <typename TRowType>
TGameStatusResult<TMap<FName, TRowType>> FStaticDataRepository::ExtractTable() const {
    const FName StructName(TRowType::StaticStruct()->GetName());
    const FStaticDataBinaryTable* Table = Tables.Find(StructName);
    if (Table == nullptr) {
        return TGameStatusResult<TMap<FName, TRowType>>::Failure(FStaticDataBinCodec::MakeError(EStaticDataBinError::TableNotFound));
    }

    const FRowPayloadFunc PayloadFunc = FindRowSerializer(StructName);
    if (PayloadFunc == nullptr) {
        return TGameStatusResult<TMap<FName, TRowType>>::Failure(FStaticDataBinCodec::MakeError(EStaticDataBinError::SerializerNotFound));
    }

    TMap<FName, TRowType> Built;
    Built.Reserve(Table->Rows.Num());
    for (const TPair<FName, TArray<uint8>>& Pair : Table->Rows) {
        TRowType Row{};
        if (!DeserializeRowWithFunc(PayloadFunc, Pair.Value, &Row)) {
            return TGameStatusResult<TMap<FName, TRowType>>::Failure(FStaticDataBinCodec::MakeError(EStaticDataBinError::RowDeserializeFailed));
        }
        Built.Add(Pair.Key, MoveTemp(Row));
    }
    return TGameStatusResult<TMap<FName, TRowType>>::Success(MoveTemp(Built));
}
