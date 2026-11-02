/**
 * @file FStaticDataRepositoryTest.cpp
 * @brief 静态数据仓库模块内自动化测试（脱机导出/反序列化往返与损坏检测），Shipping 构建自动剔除
 */

#include "StaticData/FStaticDataRepository.h"

#if WITH_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Tests/FStaticDataRepositoryTestRows.h"
#include "Utils/DataTableLoaderUtils.h"

namespace {

const TCHAR* UnitTestBinPath = TEXT("Saved/Automation/FStaticDataRepositoryTest.bin");

/**
 * @brief 读写测试行 A 的标量字段
 * @param Ar 输入或输出归档
 * @param RowPtr 指向 FRepositoryTestRowA 的行数据
 * @return 归档未出错返回 true
 */
bool HandleTestRowA(FArchive& Ar, void* RowPtr) {
    FRepositoryTestRowA& Row = *static_cast<FRepositoryTestRowA*>(RowPtr);
    Ar << Row.Score;
    Ar << Row.Weight;
    return !Ar.IsError();
}

/**
 * @brief 读写测试行 B 的字符串、数组与布尔字段
 * @param Ar 输入或输出归档
 * @param RowPtr 指向 FRepositoryTestRowB 的行数据
 * @return 归档未出错返回 true
 */
bool HandleTestRowB(FArchive& Ar, void* RowPtr) {
    FRepositoryTestRowB& Row = *static_cast<FRepositoryTestRowB*>(RowPtr);
    Ar << Row.Label;
    Ar << Row.Values;
    Ar << Row.bFlag;
    return !Ar.IsError();
}

/**
 * @brief 向仓库登记两种测试行的负载读写器
 * @param Repository 目标静态数据仓库
 */
void RegisterTestRowSerializers(FStaticDataRepository& Repository) {
    Repository.RegisterRowSerializer(FName(FRepositoryTestRowA::StaticStruct()->GetName()), &HandleTestRowA);
    Repository.RegisterRowSerializer(FName(FRepositoryTestRowB::StaticStruct()->GetName()), &HandleTestRowB);
}

/**
 * @brief 构造包含两种测试表的源仓库
 * @param OutRepository 接收读写器登记与测试数据的仓库
 * @return 两张测试表均写入成功返回 true
 */
bool BuildSourceRepository(FStaticDataRepository& OutRepository) {
    RegisterTestRowSerializers(OutRepository);
    TMap<FName, FRepositoryTestRowA> RowsA;
    RowsA.Add(FName(TEXT("Alpha")), FRepositoryTestRowA{10, 1.5f});
    RowsA.Add(FName(TEXT("Beta")), FRepositoryTestRowA{20, 2.5f});
    RowsA.Add(FName(TEXT("Gamma")), FRepositoryTestRowA{30, 3.5f});
    TMap<FName, FRepositoryTestRowB> RowsB;
    RowsB.Add(FName(TEXT("First")), FRepositoryTestRowB{TEXT("首行"), {1, 2, 3}, true});
    RowsB.Add(FName(TEXT("Second")), FRepositoryTestRowB{TEXT("次行"), {}, false});
    return OutRepository.AddTable(RowsA) && OutRepository.AddTable(RowsB);
}

/**
 * @brief 读取自动化测试使用的二进制文件
 * @param OutBytes 输出文件字节
 * @return 文件读取成功返回 true
 */
bool LoadTestBinBytes(TArray<uint8>& OutBytes) {
    const FString FilePath = FPaths::ProjectSavedDir() / UnitTestBinPath;
    return FFileHelper::LoadFileToArray(OutBytes, *FilePath);
}

/**
 * @brief 写入自动化测试使用的二进制文件
 * @param InBytes 待写入文件字节
 * @return 文件写入成功返回 true
 */
bool SaveTestBinBytes(const TArray<uint8>& InBytes) {
    const FString FilePath = FPaths::ProjectSavedDir() / UnitTestBinPath;
    return FFileHelper::SaveArrayToFile(InBytes, *FilePath);
}

/**
 * @brief 清理自动化测试生成的二进制文件
 */
void CleanupTestBinFile() {
    const FString FilePath = FPaths::ProjectSavedDir() / UnitTestBinPath;
    if (FPaths::FileExists(FilePath)) {
        IFileManager::Get().Delete(*FilePath);
    }
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRepositoryRoundTripTest, "Game.Data.Repository.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 脱机导出与再装载往返：行数/键集/关键值一致，错误码 None
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：无 UWorld/UGameInstance 的纯 C++ 场景直接构造仓库（脱机能力断言），
 * 行负载由登记读写器生成（免反射），导出 .bin 后装入全新仓库，经模板 ExtractTable 逐表比对
 */
bool FRepositoryRoundTripTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString FilePath = FPaths::ProjectSavedDir() / UnitTestBinPath;
    FStaticDataRepository Source;
    TestTrue(TEXT("源仓库两表写入成功"), BuildSourceRepository(Source));
    TestEqual(TEXT("写入两表后表数正确"), Source.TableCount(), 2);

    TestTrue(TEXT("脱机导出成功"), Source.ExportToFile(FilePath));

    FStaticDataRepository Loaded;
    TestTrue(TEXT("再装载成功"), Loaded.ImportFromFile(FilePath));
    TestEqual(TEXT("装载后错误码 None"), Loaded.GetLastError(), EStaticDataBinError::None);
    TestEqual(TEXT("装载后表数与源一致"), Loaded.TableCount(), Source.TableCount());

    RegisterTestRowSerializers(Loaded);
    TMap<FName, FRepositoryTestRowA> RowsA;
    TMap<FName, FRepositoryTestRowB> RowsB;
    TestTrue(TEXT("提取 A 表成功"), Loaded.ExtractTable(RowsA));
    TestTrue(TEXT("提取 B 表成功"), Loaded.ExtractTable(RowsB));
    TestEqual(TEXT("A 表行数一致"), RowsA.Num(), 3);
    TestEqual(TEXT("B 表行数一致"), RowsB.Num(), 2);

    const FRepositoryTestRowA* Beta = RowsA.Find(FName(TEXT("Beta")));
    const FRepositoryTestRowB* First = RowsB.Find(FName(TEXT("First")));
    TestTrue(TEXT("A 表键集完整（Beta 命中）"), Beta != nullptr);
    TestTrue(TEXT("B 表键集完整（First 命中）"), First != nullptr);
    if (Beta != nullptr) {
        TestEqual(TEXT("A 表标量关键值一致（Score）"), Beta->Score, 20);
        TestEqual(TEXT("A 表标量关键值一致（Weight）"), Beta->Weight, 2.5f);
    }
    if (First != nullptr) {
        TestEqual(TEXT("B 表字符串关键值一致（Label）"), First->Label, FString(TEXT("首行")));
        TestTrue(TEXT("B 表数组关键值一致（Values）"), First->Values == TArray<int32>({1, 2, 3}));
        TestEqual(TEXT("B 表布尔关键值一致（bFlag）"), First->bFlag, true);
    }
    CleanupTestBinFile();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRepositoryCorruptionTest, "Game.Data.Repository.Corruption",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 损坏档检测：magic/CRC/版本/结构截断逐类拒绝并置对应错误码
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 *
 * 实现思路：以 RoundTrip 产物为基线逐场景改写字节（magic 首字节、CRC 末四字节、
 * 版本字段、整体截断），断言失败装载不污染仓库既有数据（原子性）
 */
bool FRepositoryCorruptionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString FilePath = FPaths::ProjectSavedDir() / UnitTestBinPath;
    FStaticDataRepository Source;
    TestTrue(TEXT("源仓库两表写入成功"), BuildSourceRepository(Source));
    TestTrue(TEXT("基线导出成功"), Source.ExportToFile(FilePath));

    TArray<uint8> BaseBytes;
    TestTrue(TEXT("基线字节读取成功"), LoadTestBinBytes(BaseBytes));

    FStaticDataRepository Loaded;
    RegisterTestRowSerializers(Loaded);
    TArray<uint8> Tampered = BaseBytes;

    Tampered[0] ^= 0xFF;
    TestTrue(TEXT("magic 篡改档写盘成功"), SaveTestBinBytes(Tampered));
    TestFalse(TEXT("magic 篡改档装载被拒"), Loaded.ImportFromFile(FilePath));
    TestEqual(TEXT("magic 篡改错误码 MagicMismatch"), Loaded.GetLastError(), EStaticDataBinError::MagicMismatch);

    Tampered = BaseBytes;
    const int32 LastIndex = Tampered.Num() - 1;
    Tampered[LastIndex] ^= 0xFF;
    TestTrue(TEXT("CRC 篡改档写盘成功"), SaveTestBinBytes(Tampered));
    TestFalse(TEXT("CRC 篡改档装载被拒"), Loaded.ImportFromFile(FilePath));
    TestEqual(TEXT("CRC 篡改错误码 ChecksumMismatch"), Loaded.GetLastError(), EStaticDataBinError::ChecksumMismatch);

    Tampered = BaseBytes;
    const uint8 NewVersion[4] = {0x03, 0x00, 0x00, 0x00};
    Tampered[sizeof(uint32)] = NewVersion[0];
    Tampered[sizeof(uint32) + 1] = NewVersion[1];
    Tampered[sizeof(uint32) + 2] = NewVersion[2];
    Tampered[sizeof(uint32) + 3] = NewVersion[3];
    const uint32 VersionCrc = FCrc::MemCrc32(Tampered.GetData() + sizeof(uint32), Tampered.Num() - sizeof(uint32) * 2);
    FMemory::Memcpy(Tampered.GetData() + Tampered.Num() - sizeof(uint32), &VersionCrc, sizeof(VersionCrc));
    TestTrue(TEXT("版本升级档写盘成功"), SaveTestBinBytes(Tampered));
    TestFalse(TEXT("高版本档装载被拒"), Loaded.ImportFromFile(FilePath));
    TestEqual(TEXT("版本升级错误码 UnsupportedVersion"), Loaded.GetLastError(), EStaticDataBinError::UnsupportedVersion);

    TestTrue(TEXT("结构截断档写盘成功"), SaveTestBinBytes(TArray<uint8>(BaseBytes.GetData(), BaseBytes.Num() / 2)));
    TestFalse(TEXT("结构截断档装载被拒"), Loaded.ImportFromFile(FilePath));
    TestTrue(TEXT("截断错误码为 IoFailure 或 ChecksumMismatch"),
        Loaded.GetLastError() == EStaticDataBinError::IoFailure
        || Loaded.GetLastError() == EStaticDataBinError::ChecksumMismatch);
    CleanupTestBinFile();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRepositorySemanticsTest, "Game.Data.Repository.Semantics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 仓库语义：同结构覆盖写入、未登记读写器拒绝、未知表提取拒绝、Reset 复位
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FRepositorySemanticsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FStaticDataRepository Repository;
    RegisterTestRowSerializers(Repository);
    TMap<FName, FRepositoryTestRowA> Initial;
    Initial.Add(FName(TEXT("Only")), FRepositoryTestRowA{1, 0.5f});
    TestTrue(TEXT("首表写入成功"), Repository.AddTable(Initial));

    TMap<FName, FRepositoryTestRowA> Override;
    Override.Add(FName(TEXT("Replaced")), FRepositoryTestRowA{9, 9.0f});
    TestTrue(TEXT("同结构重复写入成功"), Repository.AddTable(Override));
    TestEqual(TEXT("同结构重复写入不增表数"), Repository.TableCount(), 1);

    TMap<FName, FRepositoryTestRowA> OutRows;
    TestTrue(TEXT("覆盖后提取成功"), Repository.ExtractTable(OutRows));
    TestEqual(TEXT("覆盖后行内容为末次写入"), OutRows.Num(), 1);
    TestTrue(TEXT("覆盖后旧键被替换"), OutRows.Contains(FName(TEXT("Replaced"))) && !OutRows.Contains(FName(TEXT("Only"))));

    TMap<FName, FRepositoryTestRowB> MissingOut;
    TestFalse(TEXT("未知表提取被拒"), Repository.ExtractTable(MissingOut));
    TestEqual(TEXT("未知表错误码 TableNotFound"), Repository.GetLastError(), EStaticDataBinError::TableNotFound);

    FStaticDataRepository Unregistered;
    TMap<FName, FRepositoryTestRowA> UnregisteredRows;
    UnregisteredRows.Add(FName(TEXT("Ghost")), FRepositoryTestRowA{5, 1.0f});
    TestFalse(TEXT("未登记读写器的结构写入被拒"), Unregistered.AddTable(UnregisteredRows));
    TestEqual(TEXT("未登记读写器错误码 SerializerNotFound"), Unregistered.GetLastError(), EStaticDataBinError::SerializerNotFound);
    TestEqual(TEXT("被拒写入不产生表"), Unregistered.TableCount(), 0);

    TMap<FName, FRepositoryTestRowA> UnregisteredOut;
    TestFalse(TEXT("未登记读写器的结构提取被拒"), Unregistered.ExtractTable(UnregisteredOut));

    FStaticDataRepository Forward;
    RegisterTestRowSerializers(Forward);
    TMap<FName, FRepositoryTestRowA> ForwardRows;
    ForwardRows.Add(FName(TEXT("Alpha")), FRepositoryTestRowA{1, 1.0f});
    ForwardRows.Add(FName(TEXT("Beta")), FRepositoryTestRowA{2, 2.0f});
    ForwardRows.Add(FName(TEXT("Gamma")), FRepositoryTestRowA{3, 3.0f});
    TestTrue(TEXT("正序写入成功"), Forward.AddTable(ForwardRows));

    FStaticDataRepository Reverse;
    RegisterTestRowSerializers(Reverse);
    TMap<FName, FRepositoryTestRowA> ReverseRows;
    ReverseRows.Add(FName(TEXT("Gamma")), FRepositoryTestRowA{3, 3.0f});
    ReverseRows.Add(FName(TEXT("Beta")), FRepositoryTestRowA{2, 2.0f});
    ReverseRows.Add(FName(TEXT("Alpha")), FRepositoryTestRowA{1, 1.0f});
    TestTrue(TEXT("逆序写入成功"), Reverse.AddTable(ReverseRows));

    FStaticDataRepository Modified;
    RegisterTestRowSerializers(Modified);
    TMap<FName, FRepositoryTestRowA> ModifiedRows;
    ModifiedRows.Add(FName(TEXT("Alpha")), FRepositoryTestRowA{1, 1.0f});
    ModifiedRows.Add(FName(TEXT("Beta")), FRepositoryTestRowA{2, 2.5f});
    ModifiedRows.Add(FName(TEXT("Gamma")), FRepositoryTestRowA{3, 3.0f});
    TestTrue(TEXT("差异样本写入成功"), Modified.AddTable(ModifiedRows));

    const FStaticDataContentDigest ForwardDigest = Forward.ComputeContentDigest();
    const FStaticDataContentDigest ReverseDigest = Reverse.ComputeContentDigest();
    TestEqual(TEXT("摘要表数一致"), ReverseDigest.TableCount, 1);
    TestEqual(TEXT("摘要行数一致"), ReverseDigest.RowCount, 3);
    TestTrue(TEXT("摘要非空"), !ForwardDigest.Digest.IsEmpty());
    TestEqual(TEXT("摘要与装载顺序无关"), ReverseDigest.Digest, ForwardDigest.Digest);
    TestTrue(TEXT("摘要随数据变化"), Modified.ComputeContentDigest().Digest != ForwardDigest.Digest);

    Repository.Reset();
    TestEqual(TEXT("Reset 后表数归零"), Repository.TableCount(), 0);
    TestEqual(TEXT("Reset 后错误码 None"), Repository.GetLastError(), EStaticDataBinError::None);
    TestTrue(TEXT("Reset 不影响已登记读写器"), Repository.AddTable(Initial));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDataTableLoaderUtilsTest,
    "Game.Data.DataTableLoaderUtils",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 JSON 前置失败清空输出表且有效数据正常加载
 * @param Parameters 自动化测试传入参数字符串
 * @return 测试框架入口固定返回 true
 */
bool FDataTableLoaderUtilsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString TestDirectory = FPaths::ProjectSavedDir() / TEXT("UnitTest_DataTableLoaderUtils");
    IFileManager& FileManager = IFileManager::Get();
    FileManager.DeleteDirectory(*TestDirectory, false, true);
    TestTrue(TEXT("创建 JSON 测试目录成功"), FileManager.MakeDirectory(*TestDirectory, true));

    TMap<FName, FRepositoryTestRowA> OutRows;
    FRepositoryTestRowA OldRow;
    OldRow.Score = 7;
    OldRow.Weight = 1.0f;
    OutRows.Add(FName(TEXT("Old")), OldRow);

    const FString MissingPath = TestDirectory / TEXT("Missing.json");
    TestFalse(TEXT("缺失文件加载失败"), FDataTableLoaderUtils::LoadTableFromJson(MissingPath, OutRows));
    TestEqual(TEXT("缺失文件清空预填充输出"), OutRows.Num(), 0);

    OldRow.Score = 8;
    OutRows.Add(FName(TEXT("OldAgain")), OldRow);
    const FString MalformedPath = TestDirectory / TEXT("Malformed.json");
    TestTrue(TEXT("写入损坏 JSON 成功"), FFileHelper::SaveStringToFile(TEXT("{ not valid json"), *MalformedPath));
    TestFalse(TEXT("损坏 JSON 加载失败"), FDataTableLoaderUtils::LoadTableFromJson(MalformedPath, OutRows));
    TestEqual(TEXT("损坏 JSON 清空预填充输出"), OutRows.Num(), 0);

    const FString ValidPath = TestDirectory / TEXT("Valid.json");
    const FString ValidJson = TEXT("[{\"RowName\":\"Alpha\",\"Score\":42,\"Weight\":1.5}]");
    TestTrue(TEXT("写入有效 JSON 成功"), FFileHelper::SaveStringToFile(ValidJson, *ValidPath));
    TestTrue(TEXT("有效 JSON 加载成功"), FDataTableLoaderUtils::LoadTableFromJson(ValidPath, OutRows));
    TestEqual(TEXT("有效 JSON 行数"), OutRows.Num(), 1);
    const FRepositoryTestRowA* LoadedRow = OutRows.Find(FName(TEXT("Alpha")));
    if (TestNotNull(TEXT("有效 JSON 行键正确"), LoadedRow)) {
        TestEqual(TEXT("有效 JSON 行字段正确"), LoadedRow->Score, 42);
    }

    FileManager.DeleteDirectory(*TestDirectory, false, true);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
