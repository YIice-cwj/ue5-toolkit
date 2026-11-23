/**
 * @file FStaticDataBinCodecTest.cpp
 * @brief 静态数据二进制编解码、严格版本/截断/尾字节检测、防碰撞摘要与 DataErrorCodes 映射测试
 */

#include "CoreMinimal.h"
#include "Core/DataErrorCodes.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Serialization/MemoryWriter.h"
#include "StaticData/FStaticDataBinCodec.h"

#if WITH_AUTOMATION_TESTS

namespace {

/**
 * @brief 为测试负载追加标准 Magic 与末尾 CRC32 封装为完整文件字节流
 * @param PayloadBytes 包含格式版本与表序列化数据的负载字节
 * @return 组装完成的文件字节流
 */
TArray<uint8> BuildFileBytesFromPayload(const TArray<uint8>& PayloadBytes) {
    constexpr uint32 ExpectedStaticDataBinMagic = (uint32('T') << 24) | (uint32('S') << 16) | (uint32('D') << 8) | uint32('B');
    TArray<uint8> FileBytes;
    FileBytes.Reserve(sizeof(ExpectedStaticDataBinMagic) + PayloadBytes.Num() + sizeof(uint32));
    FileBytes.Append(reinterpret_cast<const uint8*>(&ExpectedStaticDataBinMagic), sizeof(ExpectedStaticDataBinMagic));
    FileBytes.Append(PayloadBytes);
    const uint32 PayloadCrc = FCrc::MemCrc32(PayloadBytes.GetData(), PayloadBytes.Num());
    FileBytes.Append(reinterpret_cast<const uint8*>(&PayloadCrc), sizeof(PayloadCrc));
    return FileBytes;
}

/**
 * @brief 构造仅含指定版本号与 0 张表的测试文件字节流
 * @param Version 待注入的格式版本号
 * @return 带有效 Magic 与 CRC32 的文件字节流
 */
TArray<uint8> BuildVersionProbeFile(uint32 Version) {
    TArray<uint8> Payload;
    FMemoryWriter Writer(Payload);
    int32 ZeroTables = 0;
    Writer << Version << ZeroTables;
    return BuildFileBytesFromPayload(Payload);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FStaticDataBinCodecTest,
    "Game.Data.Repository.Codec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证二进制编解码、严格版本与边界校验、防拼接碰撞摘要及标准错误码映射
 * @param Parameters 自动化测试传入参数字符串
 * @return 测试框架入口固定返回 true
 */
bool FStaticDataBinCodecTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FStaticDataBinaryTable Table;
    Table.StructName = TEXT("CodecTestRow");
    Table.Rows.Add(FName(TEXT("RowA")), TArray<uint8>{1, 2, 3, 4});
    Table.Rows.Add(FName(TEXT("RowB")), TArray<uint8>{5, 6});

    FStaticDataBinaryTable EmptyTable;
    EmptyTable.StructName = TEXT("CodecTestRowEmpty");

    TMap<FName, FStaticDataBinaryTable> SourceTables;
    SourceTables.Add(FName(TEXT("CodecTestRow")), Table);
    SourceTables.Add(FName(TEXT("CodecTestRowEmpty")), EmptyTable);

    auto EncodeResult = FStaticDataBinCodec::Encode(SourceTables);
    TestTrue(TEXT("编码表集合成功"), EncodeResult.IsSuccess());
    const TArray<uint8> FileBytes = EncodeResult.IsSuccess() ? EncodeResult.ExtractValue() : TArray<uint8>();
    TestTrue(TEXT("文件字节流包含完整头与 CRC"), FileBytes.Num() > 16);

    auto DecodeResult = FStaticDataBinCodec::Decode(FileBytes);
    TestTrue(TEXT("解码成功"), DecodeResult.IsSuccess());
    const TMap<FName, FStaticDataBinaryTable> DecodedTables =
        DecodeResult.IsSuccess() ? DecodeResult.ExtractValue() : TMap<FName, FStaticDataBinaryTable>();
    TestEqual(TEXT("解码后的表数量一致"), DecodedTables.Num(), 2);

    const FStaticDataBinaryTable* DecodedRow = DecodedTables.Find(FName(TEXT("CodecTestRow")));
    if (TestNotNull(TEXT("解码后包含目标表"), DecodedRow)) {
        TestEqual(TEXT("解码后的行数一致"), DecodedRow->Rows.Num(), 2);
        const TArray<uint8>* Payload = DecodedRow->Rows.Find(FName(TEXT("RowA")));
        if (TestNotNull(TEXT("解码后包含目标行"), Payload)) {
            TestEqual(TEXT("解码后的行负载长度一致"), Payload->Num(), 4);
            TestEqual(TEXT("解码后的行负载内容一致"), static_cast<int32>((*Payload)[0]), 1);
        }
    }

    TArray<uint8> MagicCorrupted = FileBytes;
    MagicCorrupted[0] ^= 0xFF;
    const auto MagicDecodeRes = FStaticDataBinCodec::Decode(MagicCorrupted);
    if (TestTrue(TEXT("Magic 损坏应解码失败"), MagicDecodeRes.IsFailure())) {
        TestEqual(TEXT("Magic 损坏返回对应错误码"), MagicDecodeRes.GetError(), EStaticDataBinError::MagicMismatch);
    }

    TArray<uint8> PayloadCorrupted = FileBytes;
    PayloadCorrupted[FileBytes.Num() / 2] ^= 0xFF;
    const auto CrcDecodeRes = FStaticDataBinCodec::Decode(PayloadCorrupted);
    if (TestTrue(TEXT("负载损坏应解码失败"), CrcDecodeRes.IsFailure())) {
        TestEqual(TEXT("负载损坏返回 CRC 错误码"), CrcDecodeRes.GetError(), EStaticDataBinError::ChecksumMismatch);
    }

    const auto Ver0Res = FStaticDataBinCodec::Decode(BuildVersionProbeFile(0u));
    if (TestTrue(TEXT("FormatVersion=0 应失败"), Ver0Res.IsFailure())) {
        TestEqual(TEXT("FormatVersion=0 被严格拒绝"), Ver0Res.GetError(), EStaticDataBinError::UnsupportedVersion);
    }
    const auto Ver1Res = FStaticDataBinCodec::Decode(BuildVersionProbeFile(1u));
    if (TestTrue(TEXT("FormatVersion=1 应失败"), Ver1Res.IsFailure())) {
        TestEqual(TEXT("FormatVersion=1 被严格拒绝"), Ver1Res.GetError(), EStaticDataBinError::UnsupportedVersion);
    }
    const auto Ver3Res = FStaticDataBinCodec::Decode(BuildVersionProbeFile(3u));
    if (TestTrue(TEXT("FormatVersion=3 应失败"), Ver3Res.IsFailure())) {
        TestEqual(TEXT("FormatVersion=3 被严格拒绝"), Ver3Res.GetError(), EStaticDataBinError::UnsupportedVersion);
    }
    TestTrue(TEXT("FormatVersion=2 合法空表解码成功"), FStaticDataBinCodec::Decode(BuildVersionProbeFile(2u)).IsSuccess());

    TArray<uint8> TrailingGarbagePayload;
    FMemoryWriter TrailingWriter(TrailingGarbagePayload);
    uint32 ValidVersion = 2;
    int32 ZeroTables = 0;
    uint8 ExtraByte = 0x7F;
    TrailingWriter << ValidVersion << ZeroTables << ExtraByte;
    const auto TrailingRes = FStaticDataBinCodec::Decode(BuildFileBytesFromPayload(TrailingGarbagePayload));
    if (TestTrue(TEXT("末尾多余字节应失败"), TrailingRes.IsFailure())) {
        TestEqual(TEXT("末尾存在未消费多余字节返回 IoFailure"), TrailingRes.GetError(), EStaticDataBinError::IoFailure);
    }

    TArray<uint8> EmptyStructNamePayload;
    FMemoryWriter EmptyStructWriter(EmptyStructNamePayload);
    int32 OneTable = 1;
    FString EmptyName;
    int32 ZeroRows = 0;
    EmptyStructWriter << ValidVersion << OneTable << EmptyName << ZeroRows;
    const auto EmptyStructRes = FStaticDataBinCodec::Decode(BuildFileBytesFromPayload(EmptyStructNamePayload));
    if (TestTrue(TEXT("空结构名应失败"), EmptyStructRes.IsFailure())) {
        TestEqual(TEXT("空结构名返回 IoFailure"), EmptyStructRes.GetError(), EStaticDataBinError::IoFailure);
    }

    TArray<uint8> EmptyRowKeyPayload;
    FMemoryWriter EmptyRowWriter(EmptyRowKeyPayload);
    FString ValidTableName = TEXT("ValidTable");
    int32 OneRow = 1;
    int32 ZeroLen = 0;
    EmptyRowWriter << ValidVersion << OneTable << ValidTableName << OneRow << EmptyName << ZeroLen;
    const auto EmptyRowRes = FStaticDataBinCodec::Decode(BuildFileBytesFromPayload(EmptyRowKeyPayload));
    if (TestTrue(TEXT("空行键应失败"), EmptyRowRes.IsFailure())) {
        TestEqual(TEXT("空行键返回 IoFailure"), EmptyRowRes.GetError(), EStaticDataBinError::IoFailure);
    }

    TArray<uint8> NegativeRowCountPayload;
    FMemoryWriter NegRowWriter(NegativeRowCountPayload);
    int32 NegativeCount = -1;
    NegRowWriter << ValidVersion << OneTable << ValidTableName << NegativeCount;
    const auto NegRowRes = FStaticDataBinCodec::Decode(BuildFileBytesFromPayload(NegativeRowCountPayload));
    if (TestTrue(TEXT("负数行数应失败"), NegRowRes.IsFailure())) {
        TestEqual(TEXT("负数行数返回 IoFailure"), NegRowRes.GetError(), EStaticDataBinError::IoFailure);
    }

    TArray<uint8> NegativeLenPayload;
    FMemoryWriter NegLenWriter(NegativeLenPayload);
    FString ValidRowKey = TEXT("Row1");
    NegLenWriter << ValidVersion << OneTable << ValidTableName << OneRow << ValidRowKey << NegativeCount;
    const auto NegLenRes = FStaticDataBinCodec::Decode(BuildFileBytesFromPayload(NegativeLenPayload));
    if (TestTrue(TEXT("负数行负载长度应失败"), NegLenRes.IsFailure())) {
        TestEqual(TEXT("负数行负载长度返回 IoFailure"), NegLenRes.GetError(), EStaticDataBinError::IoFailure);
    }

    TArray<uint8> TruncatedBytes{1, 2, 3};
    const auto TruncRes = FStaticDataBinCodec::Decode(TruncatedBytes);
    if (TestTrue(TEXT("长度不足应失败"), TruncRes.IsFailure())) {
        TestEqual(TEXT("文件长度不足返回 IO 错误码"), TruncRes.GetError(), EStaticDataBinError::IoFailure);
    }

    TArray<uint8> BadTableCountPayload;
    FMemoryWriter BadTableWriter(BadTableCountPayload);
    BadTableWriter << ValidVersion << NegativeCount;
    const auto BadTableRes = FStaticDataBinCodec::Decode(BuildFileBytesFromPayload(BadTableCountPayload));
    if (TestTrue(TEXT("负数表数应失败"), BadTableRes.IsFailure())) {
        TestEqual(TEXT("负数表数返回 IO 错误码"), BadTableRes.GetError(), EStaticDataBinError::IoFailure);
    }

    TArray<uint8> HugeTableCountPayload;
    FMemoryWriter HugeTableWriter(HugeTableCountPayload);
    int32 HugeCount = 0x10000000;
    HugeTableWriter << ValidVersion << HugeCount;
    const auto HugeTableRes = FStaticDataBinCodec::Decode(BuildFileBytesFromPayload(HugeTableCountPayload));
    if (TestTrue(TEXT("超大表数应被上限拦截"), HugeTableRes.IsFailure())) {
        TestEqual(TEXT("超大表数返回 IoFailure"), HugeTableRes.GetError(), EStaticDataBinError::IoFailure);
    }

    TArray<uint8> HugeStringPayload;
    FMemoryWriter HugeStringWriter(HugeStringPayload);
    int32 HugeStringSaveNum = 0x7FFFFFFF;
    HugeStringWriter << ValidVersion << OneTable << HugeStringSaveNum;
    const auto HugeStringRes = FStaticDataBinCodec::Decode(BuildFileBytesFromPayload(HugeStringPayload));
    if (TestTrue(TEXT("伪造 2GB FString 长度应被 ArMaxSerializeSize 安全拦截"), HugeStringRes.IsFailure())) {
        TestEqual(TEXT("伪造超大 FString 长度返回 IoFailure"), HugeStringRes.GetError(), EStaticDataBinError::IoFailure);
    }

    TArray<uint8> DupTablePayload;
    FMemoryWriter DupTableWriter(DupTablePayload);
    int32 TwoTables = 2;
    FString SameTableName = TEXT("SameTable");
    DupTableWriter << ValidVersion << TwoTables << SameTableName << ZeroRows << SameTableName << ZeroRows;
    const auto DupTableRes = FStaticDataBinCodec::Decode(BuildFileBytesFromPayload(DupTablePayload));
    if (TestTrue(TEXT("重复表名应失败"), DupTableRes.IsFailure())) {
        TestEqual(TEXT("重复表名返回 IO 错误码"), DupTableRes.GetError(), EStaticDataBinError::IoFailure);
    }

    TArray<uint8> DupRowPayload;
    FMemoryWriter DupRowWriter(DupRowPayload);
    int32 TwoRows = 2;
    FString SameRowKey = TEXT("SameRowKey");
    DupRowWriter << ValidVersion << OneTable << ValidTableName << TwoRows << SameRowKey << ZeroLen << SameRowKey << ZeroLen;
    const auto DupRowRes = FStaticDataBinCodec::Decode(BuildFileBytesFromPayload(DupRowPayload));
    if (TestTrue(TEXT("重复行主键应失败"), DupRowRes.IsFailure())) {
        TestEqual(TEXT("同表重复行主键返回 IO 错误码"), DupRowRes.GetError(), EStaticDataBinError::IoFailure);
    }

    TMap<FName, FStaticDataBinaryTable> MismatchedStructTables;
    FStaticDataBinaryTable MismatchedTable;
    MismatchedTable.StructName = TEXT("OtherStruct");
    MismatchedStructTables.Add(FName(TEXT("KeyStruct")), MismatchedTable);
    const auto MismatchEncodeRes = FStaticDataBinCodec::Encode(MismatchedStructTables);
    if (TestTrue(TEXT("表键名与 StructName 不一致时编码被拒"), MismatchEncodeRes.IsFailure())) {
        TestEqual(TEXT("不一致结构名编码返回 IoFailure"), MismatchEncodeRes.GetError(), EStaticDataBinError::IoFailure);
    }

    TMap<FName, FStaticDataBinaryTable> EmptyTables;
    const FStaticDataContentDigest EmptyDigest = FStaticDataBinCodec::ComputeDigest(EmptyTables);
    TestEqual(TEXT("空集合摘要表数为零"), EmptyDigest.TableCount, 0);
    TestEqual(TEXT("空集合摘要行数为零"), EmptyDigest.RowCount, 0);
    TestEqual(
        TEXT("空集合摘要为 SHA1 空输入值"),
        EmptyDigest.Digest,
        FString(TEXT("da39a3ee5e6b4b0d3255bfef95601890afd80709"))
    );

    const FStaticDataContentDigest DigestA = FStaticDataBinCodec::ComputeDigest(SourceTables);
    TMap<FName, FStaticDataBinaryTable> ReorderedTables;
    ReorderedTables.Add(FName(TEXT("CodecTestRowEmpty")), EmptyTable);
    ReorderedTables.Add(FName(TEXT("CodecTestRow")), Table);
    const FStaticDataContentDigest DigestB = FStaticDataBinCodec::ComputeDigest(ReorderedTables);
    TestEqual(TEXT("摘要与装载顺序无关"), DigestA.Digest, DigestB.Digest);
    TestEqual(TEXT("摘要表数一致"), DigestA.TableCount, 2);
    TestEqual(TEXT("摘要行数一致"), DigestA.RowCount, 2);

    TMap<FName, FStaticDataBinaryTable> CollisionCaseA;
    FStaticDataBinaryTable TableAB;
    TableAB.StructName = TEXT("AB");
    TableAB.Rows.Add(FName(TEXT("C")), TArray<uint8>{1});
    CollisionCaseA.Add(FName(TEXT("AB")), TableAB);

    TMap<FName, FStaticDataBinaryTable> CollisionCaseB;
    FStaticDataBinaryTable TableA;
    TableA.StructName = TEXT("A");
    TableA.Rows.Add(FName(TEXT("BC")), TArray<uint8>{1});
    CollisionCaseB.Add(FName(TEXT("A")), TableA);
    TestTrue(TEXT("带长度前缀摘要可防止相邻字段拼接碰撞"),
        FStaticDataBinCodec::ComputeDigest(CollisionCaseA).Digest != FStaticDataBinCodec::ComputeDigest(CollisionCaseB).Digest);

    TestEqual(TEXT("DescribeError: None"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::None), FString(TEXT("无错误")));
    TestEqual(TEXT("DescribeError: IoFailure"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::IoFailure), FString(TEXT("文件读取失败或数据结构截断")));
    TestEqual(TEXT("DescribeError: MagicMismatch"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::MagicMismatch), FString(TEXT("Magic 魔数不匹配")));
    TestEqual(TEXT("DescribeError: UnsupportedVersion"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::UnsupportedVersion), FString(TEXT("格式版本不受支持")));
    TestEqual(TEXT("DescribeError: ChecksumMismatch"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::ChecksumMismatch), FString(TEXT("CRC32 校验和不匹配")));
    TestEqual(TEXT("DescribeError: TableNotFound"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::TableNotFound), FString(TEXT("未找到指定结构的数据表")));
    TestEqual(TEXT("DescribeError: RowDeserializeFailed"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::RowDeserializeFailed), FString(TEXT("行负载反序列化失败")));
    TestEqual(TEXT("DescribeError: SerializerNotFound"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::SerializerNotFound), FString(TEXT("未注册生成式负载读写器")));
    TestEqual(TEXT("DescribeError: Unknown"), FStaticDataBinCodec::DescribeError(static_cast<EStaticDataBinError>(255)), FString(TEXT("未知错误")));

    TestEqual(TEXT("ToDataErrorCode: None"), FStaticDataBinCodec::ToDataErrorCode(EStaticDataBinError::None), Game::CommonErrorCodes::Success);
    TestEqual(TEXT("ToDataErrorCode: IoFailure"), FStaticDataBinCodec::ToDataErrorCode(EStaticDataBinError::IoFailure), Game::DataErrorCodes::BinIoFailure);
    TestEqual(TEXT("ToDataErrorCode: MagicMismatch"), FStaticDataBinCodec::ToDataErrorCode(EStaticDataBinError::MagicMismatch), Game::DataErrorCodes::BinMagicMismatch);
    TestEqual(TEXT("ToDataErrorCode: UnsupportedVersion"), FStaticDataBinCodec::ToDataErrorCode(EStaticDataBinError::UnsupportedVersion), Game::DataErrorCodes::BinUnsupportedVersion);
    TestEqual(TEXT("ToDataErrorCode: ChecksumMismatch"), FStaticDataBinCodec::ToDataErrorCode(EStaticDataBinError::ChecksumMismatch), Game::DataErrorCodes::BinChecksumMismatch);
    TestEqual(TEXT("ToDataErrorCode: TableNotFound"), FStaticDataBinCodec::ToDataErrorCode(EStaticDataBinError::TableNotFound), Game::DataErrorCodes::BinTableNotFound);
    TestEqual(TEXT("ToDataErrorCode: RowDeserializeFailed"), FStaticDataBinCodec::ToDataErrorCode(EStaticDataBinError::RowDeserializeFailed), Game::DataErrorCodes::BinRowDeserializeFailed);
    TestEqual(TEXT("ToDataErrorCode: SerializerNotFound"), FStaticDataBinCodec::ToDataErrorCode(EStaticDataBinError::SerializerNotFound), Game::DataErrorCodes::BinSerializerNotFound);

    const FGameError MappedErr = FStaticDataBinCodec::MakeError(EStaticDataBinError::ChecksumMismatch);
    TestEqual(TEXT("MakeError 模块归属为 Data"), MappedErr.ModuleId, Game::ModuleId::Data);
    TestEqual(TEXT("MakeError 错误码为 BinChecksumMismatch"), MappedErr.ErrorCode, Game::DataErrorCodes::BinChecksumMismatch);
    TestFalse(TEXT("MakeError 诊断信息非空"), MappedErr.Message.IsEmpty());

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
