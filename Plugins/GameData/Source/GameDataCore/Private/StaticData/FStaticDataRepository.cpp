/**
 * @file FStaticDataRepository.cpp
 * @brief FStaticDataRepository 非模板实现（模板实现见 FStaticDataRepository.h）
 */

#include "StaticData/FStaticDataRepository.h"

#include "Logging/GameLogCategories.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

/**
 * @brief 登记某行结构的生成式负载读写器（同结构重复登记即覆盖，拒绝空名或空函数指针）
 * @param StructName 行结构名（与 TRowType::StaticStruct()->GetName() 严格一致）
 * @param PayloadFunc 行负载读写函数（由数据表生成管线产出）
 */
void FStaticDataRepository::RegisterRowSerializer(const FName& StructName, FRowPayloadFunc PayloadFunc) {
    if (StructName.IsNone() || PayloadFunc == nullptr) {
        GAME_LOG(Warning, TEXT("【静态数据仓库】忽略无效行序列化器注册: 结构体=[%s], 函数指针为空=%s。"),
            *StructName.ToString(), PayloadFunc == nullptr ? TEXT("True") : TEXT("False"));
        return;
    }
    RowPayloadFuncs.Add(StructName, PayloadFunc);
    GAME_LOG(Verbose, TEXT("【静态数据仓库】注册行序列化器: 结构体=[%s]。"), *StructName.ToString());
}

/**
 * @brief 查询当前装载表数
 * @return 表数量
 */
int32 FStaticDataRepository::TableCount() const noexcept {
    return Tables.Num();
}

/**
 * @brief 清空全部装载数据（已登记的负载读写器保留）
 */
void FStaticDataRepository::Reset() noexcept {
    Tables.Empty();
}

/**
 * @brief 导出全部装载表至 .bin 文件（脱机导出，Cook 前置；字节编码委托 FStaticDataBinCodec）
 * @param FilePath 目标文件绝对路径
 * @return 成功返回 Success；编码或写盘失败返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<void> FStaticDataRepository::ExportToFile(const FString& FilePath) const {
    TGameResult<TArray<uint8>, EStaticDataBinError> EncodeResult = FStaticDataBinCodec::Encode(Tables);
    if (EncodeResult.IsFailure()) {
        const EStaticDataBinError ErrorCode = EncodeResult.GetError();
        GAME_LOG(Error, TEXT("【静态数据仓库】导出二进制失败：编码器异常 (%s, 路径: %s)。"),
            *FStaticDataBinCodec::DescribeError(ErrorCode), *FilePath);
        return TGameStatusResult<void>::Failure(FStaticDataBinCodec::MakeError(ErrorCode));
    }

    const TArray<uint8>& FileBytes = EncodeResult.GetValue();
    if (!FFileHelper::SaveArrayToFile(FileBytes, *FilePath)) {
        GAME_LOG(Error, TEXT("【静态数据仓库】导出二进制失败：写盘 I/O 错误 (路径: %s)。"), *FilePath);
        return TGameStatusResult<void>::Failure(FStaticDataBinCodec::MakeError(EStaticDataBinError::IoFailure));
    }
    GAME_LOG(Log, TEXT("【静态数据仓库】导出二进制成功: 表数量=%d, 字节大小=%d (路径: %s)。"),
        Tables.Num(), FileBytes.Num(), *FilePath);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 自 .bin 文件装载全部表（校验 Magic / 版本 / CRC，失败保持原状）
 * @param FilePath 源文件绝对路径
 * @return 成功返回装载表数，失败返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<int32> FStaticDataRepository::ImportFromFile(const FString& FilePath) {
    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *FilePath)) {
        GAME_LOG(Warning, TEXT("【静态数据仓库】导入二进制失败：文件不存在或无法读取 (路径: %s)。"), *FilePath);
        return TGameStatusResult<int32>::Failure(FStaticDataBinCodec::MakeError(EStaticDataBinError::IoFailure));
    }

    TGameResult<TMap<FName, FStaticDataBinaryTable>, EStaticDataBinError> DecodeResult = FStaticDataBinCodec::Decode(FileBytes);
    if (DecodeResult.IsFailure()) {
        const EStaticDataBinError ErrorCode = DecodeResult.GetError();
        GAME_LOG(Warning, TEXT("【静态数据仓库】导入二进制失败：%s (路径: %s)。"),
            *FStaticDataBinCodec::DescribeError(ErrorCode), *FilePath);
        return TGameStatusResult<int32>::Failure(FStaticDataBinCodec::MakeError(ErrorCode));
    }

    Tables = DecodeResult.ExtractValue();
    GAME_LOG(Log, TEXT("【静态数据仓库】导入二进制成功: 成功装载 %d 张表 (路径: %s)。"),
        Tables.Num(), *FilePath);
    return TGameStatusResult<int32>::Success(Tables.Num());
}

/**
 * @brief 计算仓库内容摘要（与装载顺序无关，供双通道数据一致性对拍）
 * @return 表数、总行数与 SHA-1 摘要；空仓库返回 SHA-1 空输入摘要
 */
FStaticDataContentDigest FStaticDataRepository::ComputeContentDigest() const {
    return FStaticDataBinCodec::ComputeDigest(Tables);
}

/**
 * @brief 按结构体名查找已登记的行负载读写器
 * @param StructName 行结构体名
 * @return 已登记读写器函数指针，未登记返回 nullptr
 */
FStaticDataRepository::FRowPayloadFunc FStaticDataRepository::FindRowSerializer(const FName& StructName) const {
    return RowPayloadFuncs.FindRef(StructName);
}

/**
 * @brief 行结构体二进制序列化（使用调用前已解析的读写器，避免热循环重复哈希查找）
 * @param PayloadFunc 非空行负载读写器函数指针
 * @param RowPtr 行数据指针（读写器只读，不修改行数据）
 * @param OutPayload 输出二进制负载
 * @return 序列化成功返回 true，归档写出失败返回 false
 */
bool FStaticDataRepository::SerializeRowWithFunc(FRowPayloadFunc PayloadFunc, void* RowPtr, TArray<uint8>& OutPayload) {
    FMemoryWriter PayloadWriter(OutPayload, false, true);
    return PayloadFunc(PayloadWriter, RowPtr) && !PayloadWriter.IsError();
}

/**
 * @brief 行二进制负载反序列化（越界/损坏经 FMemoryReader::IsError 检出）
 * @param PayloadFunc 非空行负载读写器函数指针
 * @param InPayload 输入二进制负载
 * @param OutRowPtr 输出行数据缓冲
 * @return 反序列化成功返回 true，负载损坏返回 false
 */
bool FStaticDataRepository::DeserializeRowWithFunc(FRowPayloadFunc PayloadFunc, const TArray<uint8>& InPayload, void* OutRowPtr) {
    FMemoryReader PayloadReader(InPayload);
    PayloadReader.ArMaxSerializeSize = FMath::Max<int64>(1, InPayload.Num());
    return PayloadFunc(PayloadReader, OutRowPtr) && !PayloadReader.IsError() && PayloadReader.AtEnd();
}
