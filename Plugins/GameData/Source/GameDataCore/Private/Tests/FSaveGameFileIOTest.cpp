/**
 * @file FSaveGameFileIOTest.cpp
 * @brief 存档底层文件 I/O 自动化测试（原子写、白名单边界、Magic 校验与代次防写穿）
 */

#include "Save/FSaveGameFileIO.h"

#if WITH_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/GameSaveTestTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameFileIOTest,
    "Game.Data.Save.FileIO",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证底层文件 I/O 槽位白名单校验、原子落盘、损坏检测与 0 纪元强制写
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

    const FString TestSlot = TEXT("UnitTest_DirectFileIO");
    const FString FinalPath = FSaveGameFileIO::GetSaveSlotFilePath(TestSlot);
    const FString TempPath = FSaveGameFileIO::GetSaveSlotTempFilePath(TestSlot);
    FSaveGameFileIO::DeleteSaveSlot(TestSlot);

    UGameSaveTestArchive* Archive = NewObject<UGameSaveTestArchive>();
    Archive->Value = 12345;
    Archive->Label = TEXT("DirectFileIO");

    TArray<uint8> ValidBytes;
    TestTrue(TEXT("测试对象内存序列化成功"), FSaveGameFileIO::SerializeObject(Archive, ValidBytes));
    TestTrue(TEXT("序列化产物满足最小头要求"), ValidBytes.Num() >= 64);

    TestTrue(TEXT("0 纪元强制写入成功"), FSaveGameFileIO::WriteSlotBytes(TestSlot, ValidBytes, 0));
    TestTrue(TEXT("写入后正式文件存在"), FPaths::FileExists(FinalPath));
    TestFalse(TEXT("写入后临时文件不残留"), FPaths::FileExists(TempPath));
    TestTrue(TEXT("槽位存在性检测为 true"), FSaveGameFileIO::DoesSaveSlotExist(TestSlot));

    TArray<uint8> ReadBytes;
    TestTrue(TEXT("读取有效存档字节成功"), FSaveGameFileIO::ReadSlotBytes(TestSlot, ReadBytes));
    TestTrue(TEXT("读取数据与写入数据完全一致"), ReadBytes == ValidBytes);

    TArray<uint8> MagicCorrupted64Bytes;
    MagicCorrupted64Bytes.Init(0, 64);
    TestTrue(TEXT("写入 Magic 损坏档成功"), FFileHelper::SaveArrayToFile(MagicCorrupted64Bytes, *FinalPath));
    TestFalse(TEXT("Magic 损坏档读取被拒"), FSaveGameFileIO::ReadSlotBytes(TestSlot, ReadBytes));
    TestEqual(TEXT("读取失败时输出缓冲区清空"), ReadBytes.Num(), 0);

    TArray<uint8> CorruptedTmp10Bytes;
    CorruptedTmp10Bytes.Init(0, 10);
    TestTrue(TEXT("写入损坏临时档成功"), FFileHelper::SaveArrayToFile(CorruptedTmp10Bytes, *TempPath));
    TestFalse(TEXT("正式与临时档均损坏时读取被拒"), FSaveGameFileIO::ReadSlotBytes(TestSlot, ReadBytes));
    TestEqual(TEXT("读取失败时输出缓冲区清空"), ReadBytes.Num(), 0);

    TestTrue(TEXT("清理测试槽位成功"), FSaveGameFileIO::DeleteSaveSlot(TestSlot));
    TestFalse(TEXT("清理后正式文件不存在"), FPaths::FileExists(FinalPath));
    TestFalse(TEXT("清理后临时文件不存在"), FPaths::FileExists(TempPath));
    TestFalse(TEXT("清理后槽位存在性为 false"), FSaveGameFileIO::DoesSaveSlotExist(TestSlot));

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
