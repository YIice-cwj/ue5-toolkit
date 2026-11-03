/**
 * @file FStaticDataBinCodecTest.cpp
 * @brief 静态数据二进制编解码、损坏检测与内容摘要契约测试
 */

#include "CoreMinimal.h"
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
    constexpr uint32 StaticDataBinMagic = (uint32('T') << 24) | (uint32('S') << 16) | (uint32('D') << 8) | uint32('B');
    TArray<uint8> FileBytes;
    FileBytes.Reserve(sizeof(StaticDataBinMagic) + PayloadBytes.Num() + sizeof(uint32));
    FileBytes.Append(reinterpret_cast<const uint8*>(&StaticDataBinMagic), sizeof(StaticDataBinMagic));
    FileBytes.Append(PayloadBytes);
    const uint32 PayloadCrc = FCrc::MemCrc32(PayloadBytes.GetData(), PayloadBytes.Num());
    FileBytes.Append(reinterpret_cast<const uint8*>(&PayloadCrc), sizeof(PayloadCrc));
    return FileBytes;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FStaticDataBinCodecTest,
    "Game.Data.Repository.Codec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证二进制编解码、错误检测及顺序无关摘要
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

    TArray<uint8> FileBytes;
    TestTrue(TEXT("编码表集合成功"), FStaticDataBinCodec::Encode(SourceTables, FileBytes));
    TestTrue(TEXT("文件字节流包含完整头与 CRC"), FileBytes.Num() > 16);

    TMap<FName, FStaticDataBinaryTable> DecodedTables;
    TestTrue(TEXT("解码成功"), FStaticDataBinCodec::Decode(FileBytes, DecodedTables) == EStaticDataBinError::None);
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
    TestTrue(TEXT("Magic 损坏返回对应错误码"),
        FStaticDataBinCodec::Decode(MagicCorrupted, DecodedTables) == EStaticDataBinError::MagicMismatch);

    TArray<uint8> PayloadCorrupted = FileBytes;
    PayloadCorrupted[FileBytes.Num() / 2] ^= 0xFF;
    TestTrue(TEXT("负载损坏返回 CRC 错误码"),
        FStaticDataBinCodec::Decode(PayloadCorrupted, DecodedTables) == EStaticDataBinError::ChecksumMismatch);

    TArray<uint8> EmptyFileBytes;
    TMap<FName, FStaticDataBinaryTable> EmptyTables;
    const FStaticDataContentDigest EmptyDigest = FStaticDataBinCodec::ComputeDigest(EmptyTables);
    TestEqual(TEXT("空集合摘要表数为零"), EmptyDigest.TableCount, 0);
    TestEqual(TEXT("空集合摘要行数为零"), EmptyDigest.RowCount, 0);
    TestEqual(
        TEXT("空集合摘要为 SHA1 空输入值"),
        EmptyDigest.Digest,
        FString(TEXT("da39a3ee5e6b4b0d3255bfef95601890afd80709"))
    );
    TestTrue(TEXT("编码空表集合成功"), FStaticDataBinCodec::Encode(EmptyTables, EmptyFileBytes));

    TArray<uint8> BadVersionBytes = EmptyFileBytes;
    BadVersionBytes[4] = 0xFF;
    BadVersionBytes[5] = 0x01;
    const uint32 RecomputedCrc = FCrc::MemCrc32(
        BadVersionBytes.GetData() + sizeof(uint32),
        BadVersionBytes.Num() - sizeof(uint32) * 2);
    FMemory::Memcpy(BadVersionBytes.GetData() + BadVersionBytes.Num() - sizeof(uint32), &RecomputedCrc, sizeof(uint32));
    TestTrue(TEXT("版本过高返回对应错误码"),
        FStaticDataBinCodec::Decode(BadVersionBytes, DecodedTables) == EStaticDataBinError::UnsupportedVersion);

    TArray<uint8> TruncatedBytes{1, 2, 3};
    TestTrue(TEXT("文件长度不足返回 IO 错误码"),
        FStaticDataBinCodec::Decode(TruncatedBytes, DecodedTables) == EStaticDataBinError::IoFailure);

    TMap<FName, FStaticDataBinaryTable> PreservedTables;
    PreservedTables.Add(FName(TEXT("Preserved")), Table);
    FStaticDataBinCodec::Decode(TruncatedBytes, PreservedTables);
    TestEqual(TEXT("解码失败时输出集合保持不变"), PreservedTables.Num(), 1);

    TestEqual(TEXT("DescribeError: None"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::None), FString(TEXT("无错误")));
    TestEqual(TEXT("DescribeError: IoFailure"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::IoFailure), FString(TEXT("文件读取失败或数据结构截断")));
    TestEqual(TEXT("DescribeError: MagicMismatch"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::MagicMismatch), FString(TEXT("Magic 魔数不匹配")));
    TestEqual(TEXT("DescribeError: UnsupportedVersion"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::UnsupportedVersion), FString(TEXT("格式版本高于当前支持版本")));
    TestEqual(TEXT("DescribeError: ChecksumMismatch"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::ChecksumMismatch), FString(TEXT("CRC32 校验和不匹配")));
    TestEqual(TEXT("DescribeError: TableNotFound"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::TableNotFound), FString(TEXT("未找到指定结构的数据表")));
    TestEqual(TEXT("DescribeError: RowDeserializeFailed"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::RowDeserializeFailed), FString(TEXT("行负载反序列化失败")));
    TestEqual(TEXT("DescribeError: SerializerNotFound"), FStaticDataBinCodec::DescribeError(EStaticDataBinError::SerializerNotFound), FString(TEXT("未注册生成式负载读写器")));
    TestEqual(TEXT("DescribeError: Unknown"), FStaticDataBinCodec::DescribeError(static_cast<EStaticDataBinError>(999)), FString(TEXT("未知错误")));

    TArray<uint8> BadTableCountPayload;
    FMemoryWriter BadTableWriter(BadTableCountPayload);
    uint32 BadVersion = 2;
    int32 NegativeTableCount = -1;
    BadTableWriter << BadVersion;
    BadTableWriter << NegativeTableCount;
    TArray<uint8> BadTableBytes = BuildFileBytesFromPayload(BadTableCountPayload);
    TestTrue(TEXT("负数表数返回 IO 错误码"),
        FStaticDataBinCodec::Decode(BadTableBytes, DecodedTables) == EStaticDataBinError::IoFailure);

    TArray<uint8> DupTablePayload;
    FMemoryWriter DupTableWriter(DupTablePayload);
    int32 TwoTablesCount = 2;
    DupTableWriter << BadVersion;
    DupTableWriter << TwoTablesCount;
    FString SameTableName = TEXT("SameTable");
    int32 ZeroRows = 0;
    DupTableWriter << SameTableName << ZeroRows;
    DupTableWriter << SameTableName << ZeroRows;
    TArray<uint8> DupTableBytes = BuildFileBytesFromPayload(DupTablePayload);
    TestTrue(TEXT("重复表名返回 IO 错误码"),
        FStaticDataBinCodec::Decode(DupTableBytes, DecodedTables) == EStaticDataBinError::IoFailure);

    TArray<uint8> DupRowPayload;
    FMemoryWriter DupRowWriter(DupRowPayload);
    int32 SingleTableCount = 1;
    DupRowWriter << BadVersion;
    DupRowWriter << SingleTableCount;
    FString SingleTableName = TEXT("TableWithDupRows");
    int32 TwoRowsCount = 2;
    DupRowWriter << SingleTableName << TwoRowsCount;
    FString SameRowKey = TEXT("SameRowKey");
    int32 EmptyPayloadLen = 0;
    DupRowWriter << SameRowKey << EmptyPayloadLen;
    DupRowWriter << SameRowKey << EmptyPayloadLen;
    TArray<uint8> DupRowBytes = BuildFileBytesFromPayload(DupRowPayload);
    TestTrue(TEXT("同表重复行主键返回 IO 错误码"),
        FStaticDataBinCodec::Decode(DupRowBytes, DecodedTables) == EStaticDataBinError::IoFailure);

    const FStaticDataContentDigest DigestA = FStaticDataBinCodec::ComputeDigest(SourceTables);
    TMap<FName, FStaticDataBinaryTable> ReorderedTables;
    ReorderedTables.Add(FName(TEXT("CodecTestRowEmpty")), EmptyTable);
    ReorderedTables.Add(FName(TEXT("CodecTestRow")), Table);
    const FStaticDataContentDigest DigestB = FStaticDataBinCodec::ComputeDigest(ReorderedTables);
    TestEqual(TEXT("摘要与装载顺序无关"), DigestA.Digest, DigestB.Digest);
    TestEqual(TEXT("摘要表数一致"), DigestA.TableCount, 2);
    TestEqual(TEXT("摘要行数一致"), DigestA.RowCount, 2);

    return true;
}

#endif
