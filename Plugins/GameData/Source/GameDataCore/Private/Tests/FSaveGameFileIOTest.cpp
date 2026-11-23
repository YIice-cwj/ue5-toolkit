/**
 * @file FSaveGameFileIOTest.cpp
 * @brief 存档底层文件 I/O 自动化测试（原子写、白名单边界、代次防写穿、删槽失效与多线程并发隔离）
 */

#include "Save/FSaveGameFileIO.h"

#if WITH_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "Core/DataErrorCodes.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/GameSaveTestTypes.h"

#include <atomic>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameFileIOTest,
    "Game.Data.Save.FileIO",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证底层文件 I/O 槽位白名单校验、原子落盘、损坏检测、代次防写穿与并发槽位隔离
 * @param Parameters 自动化测试参数
 * @return 测试框架入口固定返回 true
 */
bool FSaveGameFileIOTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const TSet<FString> AllowedSlots{ TEXT("ValidSlotA"), TEXT("ValidSlotB") };

    TestTrue(TEXT("白名单槽位允许"), FSaveGameFileIO::IsSlotNameAllowed(TEXT("ValidSlotA"), AllowedSlots));
    TestFalse(TEXT("白名单外槽位拒绝"), FSaveGameFileIO::IsSlotNameAllowed(TEXT("UnknownSlot"), AllowedSlots));
    TestFalse(TEXT("空槽位名拒绝"), FSaveGameFileIO::IsSlotNameAllowed(TEXT(""), AllowedSlots));
    TestFalse(TEXT("超长槽位名拒绝"), FSaveGameFileIO::IsSlotNameAllowed(FString::ChrN(65, 'S'), AllowedSlots));
    TestFalse(TEXT("正斜杠路径穿越拒绝"), FSaveGameFileIO::IsSlotNameAllowed(TEXT("../Evil"), AllowedSlots));
    TestFalse(TEXT("反斜杠路径穿越拒绝"), FSaveGameFileIO::IsSlotNameAllowed(TEXT("Evil\\Path"), AllowedSlots));
    TestFalse(TEXT("包含句点槽位名拒绝"), FSaveGameFileIO::IsSlotNameAllowed(TEXT("Slot.Sav"), AllowedSlots));

    const TSet<FString> EmptyAllowedSet;
    TestTrue(TEXT("空白名单时合法槽位放行"), FSaveGameFileIO::IsSlotNameAllowed(TEXT("AnyNormalSlot"), EmptyAllowedSet));
    TestFalse(TEXT("含冒号槽位名拒绝"), FSaveGameFileIO::IsSlotNameAllowed(TEXT("C:Slot"), EmptyAllowedSet));
    TestFalse(TEXT("含星号槽位名拒绝"), FSaveGameFileIO::IsSlotNameAllowed(TEXT("Slot*"), EmptyAllowedSet));
    TestFalse(TEXT("含问号槽位名拒绝"), FSaveGameFileIO::IsSlotNameAllowed(TEXT("Slot?"), EmptyAllowedSet));
    TestFalse(TEXT("含引号槽位名拒绝"), FSaveGameFileIO::IsSlotNameAllowed(TEXT("Slot\"A"), EmptyAllowedSet));
    TestFalse(TEXT("含尖括号槽位名拒绝"), FSaveGameFileIO::IsSlotNameAllowed(TEXT("Slot<A>"), EmptyAllowedSet));
    TestFalse(TEXT("含竖线槽位名拒绝"), FSaveGameFileIO::IsSlotNameAllowed(TEXT("Slot|A"), EmptyAllowedSet));
    TestFalse(TEXT("含前导空格槽位名拒绝"), FSaveGameFileIO::IsSlotNameAllowed(TEXT(" LeadingSpace"), EmptyAllowedSet));
    TestFalse(TEXT("含尾随空格槽位名拒绝"), FSaveGameFileIO::IsSlotNameAllowed(TEXT("TrailingSpace "), EmptyAllowedSet));
    TestFalse(TEXT("含控制字符槽位名拒绝"), FSaveGameFileIO::IsSlotNameAllowed(TEXT("Slot\nName"), EmptyAllowedSet));

    const auto NullSerializeRes = FSaveGameFileIO::SerializeObject(nullptr);
    if (TestTrue(TEXT("空对象序列化失败"), NullSerializeRes.IsFailure())) {
        TestEqual(TEXT("空对象序列化模块归属为 Data"), NullSerializeRes.GetError().ModuleId, Game::ModuleId::Data);
        TestEqual(TEXT("空对象序列化错误码为 InvalidSaveObject"), NullSerializeRes.GetError().ErrorCode, Game::DataErrorCodes::InvalidSaveObject);
    }

    const FString NonExistentSlot = TEXT("NonExistentSlot_99999");
    const auto NonExistentReadRes = FSaveGameFileIO::ReadSlotBytes(NonExistentSlot);
    if (TestTrue(TEXT("不存在槽位读取失败"), NonExistentReadRes.IsFailure())) {
        TestEqual(TEXT("不存在槽位读取错误码为 SaveSlotNotFound"), NonExistentReadRes.GetError().ErrorCode, Game::DataErrorCodes::SaveSlotNotFound);
    }

    const auto NonExistentDelRes = FSaveGameFileIO::DeleteSaveSlot(NonExistentSlot);
    if (TestTrue(TEXT("不存在槽位删除失败"), NonExistentDelRes.IsFailure())) {
        TestEqual(TEXT("不存在槽位删除错误码为 SaveSlotNotFound"), NonExistentDelRes.GetError().ErrorCode, Game::DataErrorCodes::SaveSlotNotFound);
    }

    const FString TestSlot = TEXT("UnitTest_DirectFileIO");
    const FString FinalPath = FSaveGameFileIO::GetSaveSlotFilePath(TestSlot);
    const FString TempPath = FSaveGameFileIO::GetSaveSlotTempFilePath(TestSlot);
    (void)FSaveGameFileIO::DeleteSaveSlot(TestSlot);

    UGameSaveTestArchive* ArchiveV1 = NewObject<UGameSaveTestArchive>();
    ArchiveV1->Value = 12345;
    ArchiveV1->Label = TEXT("Gen1Payload");
    auto SerializeV1 = FSaveGameFileIO::SerializeObject(ArchiveV1);
    TestTrue(TEXT("V1 对象内存序列化成功"), SerializeV1.IsSuccess());
    const TArray<uint8> BytesV1 = SerializeV1.IsSuccess() ? SerializeV1.ExtractValue() : TArray<uint8>();
    TestTrue(TEXT("序列化产物满足最小头要求"), BytesV1.Num() >= 64);

    UGameSaveTestArchive* ArchiveV2 = NewObject<UGameSaveTestArchive>();
    ArchiveV2->Value = 67890;
    ArchiveV2->Label = TEXT("Gen2Payload_Newer");
    auto SerializeV2 = FSaveGameFileIO::SerializeObject(ArchiveV2);
    TestTrue(TEXT("V2 对象内存序列化成功"), SerializeV2.IsSuccess());
    const TArray<uint8> BytesV2 = SerializeV2.IsSuccess() ? SerializeV2.ExtractValue() : TArray<uint8>();

    TestTrue(TEXT("0 纪元强制写入成功"), FSaveGameFileIO::WriteSlotBytes(TestSlot, BytesV1, 0).IsSuccess());
    TestTrue(TEXT("写入后正式文件存在"), FPaths::FileExists(FinalPath));
    TestFalse(TEXT("写入后临时文件不残留"), FPaths::FileExists(TempPath));
    TestTrue(TEXT("槽位存在性检测为 true"), FSaveGameFileIO::DoesSaveSlotExist(TestSlot));

    const auto ReadResult = FSaveGameFileIO::ReadSlotBytes(TestSlot);
    TestTrue(TEXT("读取有效存档字节成功"), ReadResult.IsSuccess());
    if (ReadResult.IsSuccess()) {
        TestTrue(TEXT("读取数据与写入数据完全一致"), ReadResult.GetValue() == BytesV1);
    }

    const uint64 Gen1 = FSaveGameFileIO::IssueSlotSaveGeneration(TestSlot);
    const uint64 Gen2 = FSaveGameFileIO::IssueSlotSaveGeneration(TestSlot);
    TestTrue(TEXT("代次应严格递增"), Gen2 > Gen1);
    bool bSkippedNew = true;
    TestTrue(TEXT("新代次 Gen2 写入成功"), FSaveGameFileIO::WriteSlotBytes(TestSlot, BytesV2, Gen2, &bSkippedNew).IsSuccess());
    TestFalse(TEXT("新代次 Gen2 不应标记为过期跳过"), bSkippedNew);
    bool bSkippedStale = false;
    TestTrue(TEXT("旧代次 Gen1 过期写应平滑跳过返回 Success"), FSaveGameFileIO::WriteSlotBytes(TestSlot, BytesV1, Gen1, &bSkippedStale).IsSuccess());
    TestTrue(TEXT("旧代次 Gen1 应标记 bOutSkippedStale=true"), bSkippedStale);
    const auto AfterStaleWrite = FSaveGameFileIO::ReadSlotBytes(TestSlot);
    TestTrue(TEXT("旧代次写穿拦截后磁盘仍保留 Gen2 内容"), AfterStaleWrite.IsSuccess() && AfterStaleWrite.GetValue() == BytesV2);

    const uint64 GenBeforeInvalidate = FSaveGameFileIO::IssueSlotSaveGeneration(TestSlot);
    FSaveGameFileIO::InvalidateSlotSaveGeneration(TestSlot);
    bool bSkippedAfterInvalidate = false;
    TestTrue(TEXT("InvalidateSlotSaveGeneration 后旧代次写应被跳过"),
        FSaveGameFileIO::WriteSlotBytes(TestSlot, BytesV1, GenBeforeInvalidate, &bSkippedAfterInvalidate).IsSuccess());
    TestTrue(TEXT("InvalidateSlotSaveGeneration 后 bOutSkippedStale 应为 true"), bSkippedAfterInvalidate);

    const uint64 GenBeforeDelete = FSaveGameFileIO::IssueSlotSaveGeneration(TestSlot);
    TestTrue(TEXT("删除槽位成功并失效旧代次"), FSaveGameFileIO::DeleteSaveSlot(TestSlot).IsSuccess());
    TestTrue(TEXT("删槽后旧代次写入应被丢弃"), FSaveGameFileIO::WriteSlotBytes(TestSlot, BytesV1, GenBeforeDelete).IsSuccess());
    TestFalse(TEXT("旧代次写入不得复活已删除槽位"), FSaveGameFileIO::DoesSaveSlotExist(TestSlot));

    TArray<uint8> MagicCorrupted64Bytes;
    MagicCorrupted64Bytes.Init(0, 64);
    TestTrue(TEXT("写入 Magic 损坏档成功"), FFileHelper::SaveArrayToFile(MagicCorrupted64Bytes, *FinalPath));
    const auto MagicCorruptRes = FSaveGameFileIO::ReadSlotBytes(TestSlot);
    if (TestTrue(TEXT("Magic 损坏档读取被拒"), MagicCorruptRes.IsFailure())) {
        TestEqual(TEXT("Magic 损坏档错误码为 CorruptedSaveFile"), MagicCorruptRes.GetError().ErrorCode, Game::DataErrorCodes::CorruptedSaveFile);
    }

    TArray<uint8> CorruptedTmp10Bytes;
    CorruptedTmp10Bytes.Init(0, 10);
    TestTrue(TEXT("写入损坏临时档成功"), FFileHelper::SaveArrayToFile(CorruptedTmp10Bytes, *TempPath));
    TestTrue(TEXT("正式与临时档均损坏时读取被拒"), FSaveGameFileIO::ReadSlotBytes(TestSlot).IsFailure());

    TestTrue(TEXT("清理测试槽位成功"), FSaveGameFileIO::DeleteSaveSlot(TestSlot).IsSuccess());
    TestFalse(TEXT("清理后正式文件不存在"), FPaths::FileExists(FinalPath));
    TestFalse(TEXT("清理后临时文件不存在"), FPaths::FileExists(TempPath));
    TestFalse(TEXT("清理后槽位存在性为 false"), FSaveGameFileIO::DoesSaveSlotExist(TestSlot));

    constexpr int32 ParallelSlots = 4;
    constexpr int32 IterationsPerSlot = 20;
    std::atomic<int32> ConcurrentFailures{0};
    ParallelFor(ParallelSlots, [&BytesV1, &BytesV2, &ConcurrentFailures](int32 WorkerIndex) {
        const FString Slot = FString::Printf(TEXT("UnitTest_ConcurrentSlot_%d"), WorkerIndex);
        for (int32 Iter = 0; Iter < IterationsPerSlot; ++Iter) {
            const TArray<uint8>& Payload = (Iter % 2 == 0) ? BytesV1 : BytesV2;
            const uint64 Gen = FSaveGameFileIO::IssueSlotSaveGeneration(Slot);
            if (FSaveGameFileIO::WriteSlotBytes(Slot, Payload, Gen).IsFailure()) {
                ConcurrentFailures.fetch_add(1);
                break;
            }
            const auto ReadBack = FSaveGameFileIO::ReadSlotBytes(Slot);
            if (ReadBack.IsFailure() || ReadBack.GetValue() != Payload) {
                ConcurrentFailures.fetch_add(1);
                break;
            }
        }
        (void)FSaveGameFileIO::DeleteSaveSlot(Slot);
    });
    TestEqual(TEXT("多槽位并发读写零错误"), ConcurrentFailures.load(), 0);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
