/**
 * @file FStaticDataRepositoryTest.cpp
 * @brief 静态数据仓库模块内自动化测试（脱机导出/反序列化往返、损坏检测与统一错误码契约）
 */

#include "StaticData/FStaticDataRepository.h"

#if WITH_AUTOMATION_TESTS

#include "Core/DataErrorCodes.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/FStaticDataRepositoryTestRows.h"

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
 * @brief 始终返回序列化失败的测试读写器
 * @param Ar 归档对象
 * @param RowPtr 行数据指针
 * @return 固定返回 false
 */
bool HandleFailingSerialize(FArchive& Ar, void* RowPtr) {
    (void)Ar;
    (void)RowPtr;
    return false;
}

/**
 * @brief 写入成功但在反序列化时失败的测试读写器
 * @param Ar 归档对象
 * @param RowPtr 行数据指针
 * @return 保存返回 true，反序列化返回 false
 */
bool HandleFailingDeserialize(FArchive& Ar, void* RowPtr) {
    if (Ar.IsSaving()) {
        return HandleTestRowA(Ar, RowPtr);
    }
    return false;
}

/**
 * @brief 写入多出尾部冗余字节的测试读写器（验证反序列化尾字节拦截）
 * @param Ar 归档对象
 * @param RowPtr 行数据指针
 * @return 归档未出错返回 true
 */
bool HandleTrailingBytesSerialize(FArchive& Ar, void* RowPtr) {
    if (!HandleTestRowA(Ar, RowPtr)) {
        return false;
    }
    if (Ar.IsSaving()) {
        uint8 ExtraByte = 0xAB;
        Ar << ExtraByte;
    }
    return !Ar.IsError();
}

/**
 * @brief 向仓库登记两种测试行的负载读写器（验证模板重载接口）
 * @param Repository 目标静态数据仓库
 */
void RegisterTestRowSerializers(FStaticDataRepository& Repository) {
    Repository.RegisterRowSerializer<FRepositoryTestRowA>(&HandleTestRowA);
    Repository.RegisterRowSerializer<FRepositoryTestRowB>(&HandleTestRowB);
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
    return OutRepository.AddTable(RowsA).IsSuccess() && OutRepository.AddTable(RowsB).IsSuccess();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRepositoryRoundTripTest,
    "Game.Data.Repository.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 脱机导出与再装载往返：行数/键集/关键值一致，支持拷贝与移动语义
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FRepositoryRoundTripTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString FilePath = FPaths::ProjectSavedDir() / UnitTestBinPath;
    FStaticDataRepository Source;
    TestTrue(TEXT("源仓库两表写入成功"), BuildSourceRepository(Source));
    TestEqual(TEXT("写入两表后表数正确"), Source.TableCount(), 2);

    FStaticDataRepository Copied(Source);
    TestEqual(TEXT("拷贝构造后表数一致"), Copied.TableCount(), 2);
    TestEqual(TEXT("拷贝构造后内容摘要一致"), Copied.ComputeContentDigest().Digest, Source.ComputeContentDigest().Digest);

    FStaticDataRepository Moved(MoveTemp(Copied));
    TestEqual(TEXT("移动构造后表数一致"), Moved.TableCount(), 2);

    TestTrue(TEXT("脱机导出成功"), Source.ExportToFile(FilePath).IsSuccess());

    FStaticDataRepository Loaded;
    const TGameStatusResult<int32> ImportRes = Loaded.ImportFromFile(FilePath);
    TestTrue(TEXT("再装载成功"), ImportRes.IsSuccess());
    if (ImportRes.IsSuccess()) {
        TestEqual(TEXT("再装载返回表数与源一致"), ImportRes.GetValue(), Source.TableCount());
    }
    TestEqual(TEXT("装载后表数与源一致"), Loaded.TableCount(), Source.TableCount());

    RegisterTestRowSerializers(Loaded);
    auto ExtractARes = Loaded.ExtractTable<FRepositoryTestRowA>();
    TestTrue(TEXT("提取 A 表成功"), ExtractARes.IsSuccess());
    TMap<FName, FRepositoryTestRowA> RowsA = ExtractARes.IsSuccess() ? ExtractARes.ExtractValue() : TMap<FName, FRepositoryTestRowA>();

    auto ExtractBRes = Loaded.ExtractTable<FRepositoryTestRowB>();
    TestTrue(TEXT("提取 B 表成功"), ExtractBRes.IsSuccess());
    TMap<FName, FRepositoryTestRowB> RowsB = ExtractBRes.IsSuccess() ? ExtractBRes.ExtractValue() : TMap<FName, FRepositoryTestRowB>();
    TestEqual(TEXT("A 表行数一致"), RowsA.Num(), 3);
    TestEqual(TEXT("B 表行数一致"), RowsB.Num(), 2);

    const FRepositoryTestRowA* Beta = RowsA.Find(FName(TEXT("Beta")));
    const FRepositoryTestRowB* First = RowsB.Find(FName(TEXT("First")));
    if (TestNotNull(TEXT("A 表键集完整（Beta 命中）"), Beta)) {
        TestEqual(TEXT("A 表标量关键值一致（Score）"), Beta->Score, 20);
        TestEqual(TEXT("A 表标量关键值一致（Weight）"), Beta->Weight, 2.5f);
    }
    if (TestNotNull(TEXT("B 表键集完整（First 命中）"), First)) {
        TestEqual(TEXT("B 表字符串关键值一致（Label）"), First->Label, FString(TEXT("首行")));
        TestTrue(TEXT("B 表数组关键值一致（Values）"), First->Values == TArray<int32>({1, 2, 3}));
        TestEqual(TEXT("B 表布尔关键值一致（bFlag）"), First->bFlag, true);
    }
    CleanupTestBinFile();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRepositoryCorruptionTest,
    "Game.Data.Repository.Corruption",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 损坏档检测：magic/CRC/版本/结构截断逐类拒绝并返回统一 Game::DataErrorCodes，失败不污染已有数据
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FRepositoryCorruptionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString FilePath = FPaths::ProjectSavedDir() / UnitTestBinPath;
    FStaticDataRepository Source;
    TestTrue(TEXT("源仓库两表写入成功"), BuildSourceRepository(Source));
    TestTrue(TEXT("基线导出成功"), Source.ExportToFile(FilePath).IsSuccess());

    TArray<uint8> BaseBytes;
    TestTrue(TEXT("基线字节读取成功"), LoadTestBinBytes(BaseBytes));

    FStaticDataRepository Loaded;
    TestTrue(TEXT("预载基线数据成功"), Loaded.ImportFromFile(FilePath).IsSuccess());
    TestEqual(TEXT("预载基线表数为 2"), Loaded.TableCount(), 2);

    TArray<uint8> Tampered = BaseBytes;
    Tampered[0] ^= 0xFF;
    TestTrue(TEXT("magic 篡改档写盘成功"), SaveTestBinBytes(Tampered));
    const auto MagicRes = Loaded.ImportFromFile(FilePath);
    if (TestTrue(TEXT("magic 篡改档装载被拒"), MagicRes.IsFailure())) {
        TestEqual(TEXT("magic 篡改模块归属为 Data"), MagicRes.GetError().ModuleId, Game::ModuleId::Data);
        TestEqual(TEXT("magic 篡改错误码 BinMagicMismatch"), MagicRes.GetError().ErrorCode, Game::DataErrorCodes::BinMagicMismatch);
    }
    TestEqual(TEXT("装载失败不破坏仓库既有表数据"), Loaded.TableCount(), 2);

    Tampered = BaseBytes;
    Tampered[Tampered.Num() - 1] ^= 0xFF;
    TestTrue(TEXT("CRC 篡改档写盘成功"), SaveTestBinBytes(Tampered));
    const auto CrcRes = Loaded.ImportFromFile(FilePath);
    if (TestTrue(TEXT("CRC 篡改档装载被拒"), CrcRes.IsFailure())) {
        TestEqual(TEXT("CRC 篡改错误码 BinChecksumMismatch"), CrcRes.GetError().ErrorCode, Game::DataErrorCodes::BinChecksumMismatch);
    }

    Tampered = BaseBytes;
    const uint8 NewVersion[4] = {0x03, 0x00, 0x00, 0x00};
    FMemory::Memcpy(Tampered.GetData() + sizeof(uint32), NewVersion, sizeof(NewVersion));
    const uint32 VersionCrc = FCrc::MemCrc32(Tampered.GetData() + sizeof(uint32), Tampered.Num() - sizeof(uint32) * 2);
    FMemory::Memcpy(Tampered.GetData() + Tampered.Num() - sizeof(uint32), &VersionCrc, sizeof(VersionCrc));
    TestTrue(TEXT("版本升级档写盘成功"), SaveTestBinBytes(Tampered));
    const auto VersionRes = Loaded.ImportFromFile(FilePath);
    if (TestTrue(TEXT("高版本档装载被拒"), VersionRes.IsFailure())) {
        TestEqual(TEXT("版本升级错误码 BinUnsupportedVersion"), VersionRes.GetError().ErrorCode, Game::DataErrorCodes::BinUnsupportedVersion);
    }

    TestTrue(TEXT("结构截断档写盘成功"), SaveTestBinBytes(TArray<uint8>(BaseBytes.GetData(), BaseBytes.Num() / 2)));
    const auto TruncRes = Loaded.ImportFromFile(FilePath);
    if (TestTrue(TEXT("结构截断档装载被拒"), TruncRes.IsFailure())) {
        TestTrue(TEXT("截断错误码为 BinIoFailure 或 BinChecksumMismatch"),
            TruncRes.GetError().ErrorCode == Game::DataErrorCodes::BinIoFailure
            || TruncRes.GetError().ErrorCode == Game::DataErrorCodes::BinChecksumMismatch);
    }

    const auto MissingRes = Loaded.ImportFromFile(FPaths::ProjectSavedDir() / TEXT("NonExistent_StaticData.bin"));
    if (TestTrue(TEXT("缺失文件导入被拒"), MissingRes.IsFailure())) {
        TestEqual(TEXT("缺失文件错误码 BinIoFailure"), MissingRes.GetError().ErrorCode, Game::DataErrorCodes::BinIoFailure);
    }

    AddExpectedErrorPlain(TEXT("写盘 I/O 错误"), EAutomationExpectedErrorFlags::Contains, 1);
    const auto BadExportRes = Source.ExportToFile(TEXT("/NonExistentDirectory/Forbidden/Test.bin"));
    if (TestTrue(TEXT("非法路径导出被拒"), BadExportRes.IsFailure())) {
        TestEqual(TEXT("非法路径导出错误码 BinIoFailure"), BadExportRes.GetError().ErrorCode, Game::DataErrorCodes::BinIoFailure);
    }

    CleanupTestBinFile();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRepositorySemanticsTest,
    "Game.Data.Repository.Semantics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 仓库语义：同结构覆盖写入、空表未登记读写器拒绝、非法注册拒绝、未知表提取拒绝与 Reset 复位
 * @param Parameters 自动化框架参数（未使用）
 * @return 断言全部通过返回 true
 */
bool FRepositorySemanticsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FStaticDataRepository Repository;
    Repository.RegisterRowSerializer(NAME_None, &HandleTestRowA);
    Repository.RegisterRowSerializer(FName(FRepositoryTestRowA::StaticStruct()->GetName()), nullptr);
    TMap<FName, FRepositoryTestRowA> EmptyRows;
    const auto InvalidRegRes = Repository.AddTable(EmptyRows);
    if (TestTrue(TEXT("空名或空函数指针注册被忽略，空表写入仍被拒"), InvalidRegRes.IsFailure())) {
        TestEqual(TEXT("空表未登记读写器返回 BinSerializerNotFound"),
            InvalidRegRes.GetError().ErrorCode, Game::DataErrorCodes::BinSerializerNotFound);
    }

    RegisterTestRowSerializers(Repository);
    TestTrue(TEXT("已登记读写器时合法空表写入成功"), Repository.AddTable(EmptyRows).IsSuccess());
    TestEqual(TEXT("写入空表后表数为 1"), Repository.TableCount(), 1);

    TMap<FName, FRepositoryTestRowA> NoneKeyRows;
    NoneKeyRows.Add(NAME_None, FRepositoryTestRowA{1, 1.0f});
    const auto NoneKeyRes = Repository.AddTable(NoneKeyRows);
    if (TestTrue(TEXT("包含 NAME_None 主键的表写入被拒"), NoneKeyRes.IsFailure())) {
        TestEqual(TEXT("NAME_None 主键错误码 BinIoFailure"), NoneKeyRes.GetError().ErrorCode, Game::DataErrorCodes::BinIoFailure);
    }

    TMap<FName, FRepositoryTestRowA> Initial;
    Initial.Add(FName(TEXT("Only")), FRepositoryTestRowA{1, 0.5f});
    TestTrue(TEXT("首表写入成功"), Repository.AddTable(Initial).IsSuccess());

    TMap<FName, FRepositoryTestRowA> Override;
    Override.Add(FName(TEXT("Replaced")), FRepositoryTestRowA{9, 9.0f});
    TestTrue(TEXT("同结构重复写入成功"), Repository.AddTable(Override).IsSuccess());
    TestEqual(TEXT("同结构重复写入不增表数"), Repository.TableCount(), 1);

    auto ExtractRes = Repository.ExtractTable<FRepositoryTestRowA>();
    TestTrue(TEXT("覆盖后提取成功"), ExtractRes.IsSuccess());
    if (ExtractRes.IsSuccess()) {
        const auto& OutRows = ExtractRes.GetValue();
        TestEqual(TEXT("覆盖后行内容为末次写入"), OutRows.Num(), 1);
        TestTrue(TEXT("覆盖后旧键被替换"), OutRows.Contains(FName(TEXT("Replaced"))) && !OutRows.Contains(FName(TEXT("Only"))));
    }

    const auto UnknownRes = Repository.ExtractTable<FRepositoryTestRowB>();
    if (TestTrue(TEXT("未知表提取被拒"), UnknownRes.IsFailure())) {
        TestEqual(TEXT("未知表错误码 BinTableNotFound"), UnknownRes.GetError().ErrorCode, Game::DataErrorCodes::BinTableNotFound);
    }

    const FString FilePath = FPaths::ProjectSavedDir() / UnitTestBinPath;
    TestTrue(TEXT("导出用于未登记提取测试成功"), Repository.ExportToFile(FilePath).IsSuccess());
    FStaticDataRepository LoadedWithoutSerializer;
    TestTrue(TEXT("不登记读写器可导入原始字节镜像"), LoadedWithoutSerializer.ImportFromFile(FilePath).IsSuccess());
    const auto UnregExtractRes = LoadedWithoutSerializer.ExtractTable<FRepositoryTestRowA>();
    if (TestTrue(TEXT("已存在表但未登记读写器时提取被拒"), UnregExtractRes.IsFailure())) {
        TestEqual(TEXT("未登记读写器提取错误码 BinSerializerNotFound"),
            UnregExtractRes.GetError().ErrorCode, Game::DataErrorCodes::BinSerializerNotFound);
    }
    CleanupTestBinFile();

    FStaticDataRepository FailSerializerRepo;
    FailSerializerRepo.RegisterRowSerializer<FRepositoryTestRowA>(&HandleFailingSerialize);
    const auto FailAddRes = FailSerializerRepo.AddTable(Initial);
    if (TestTrue(TEXT("序列化器失败导致写入被拒"), FailAddRes.IsFailure())) {
        TestEqual(TEXT("序列化器失败错误码 BinIoFailure"), FailAddRes.GetError().ErrorCode, Game::DataErrorCodes::BinIoFailure);
    }

    FStaticDataRepository FailDeserializeRepo;
    FailDeserializeRepo.RegisterRowSerializer<FRepositoryTestRowA>(&HandleFailingDeserialize);
    TestTrue(TEXT("反序列化测试源表写入成功"), FailDeserializeRepo.AddTable(Initial).IsSuccess());
    const auto FailExtractRes = FailDeserializeRepo.ExtractTable<FRepositoryTestRowA>();
    if (TestTrue(TEXT("反序列化器失败导致提取被拒"), FailExtractRes.IsFailure())) {
        TestEqual(TEXT("反序列化器失败错误码 BinRowDeserializeFailed"),
            FailExtractRes.GetError().ErrorCode, Game::DataErrorCodes::BinRowDeserializeFailed);
    }

    FStaticDataRepository TrailingRepo;
    TrailingRepo.RegisterRowSerializer<FRepositoryTestRowA>(&HandleTrailingBytesSerialize);
    TestTrue(TEXT("带尾字节行表写入成功"), TrailingRepo.AddTable(Initial).IsSuccess());
    const auto TrailingExtractRes = TrailingRepo.ExtractTable<FRepositoryTestRowA>();
    if (TestTrue(TEXT("行负载含未消费尾字节时提取被拒"), TrailingExtractRes.IsFailure())) {
        TestEqual(TEXT("行负载尾字节错误码 BinRowDeserializeFailed"),
            TrailingExtractRes.GetError().ErrorCode, Game::DataErrorCodes::BinRowDeserializeFailed);
    }

    FStaticDataRepository Forward;
    RegisterTestRowSerializers(Forward);
    TMap<FName, FRepositoryTestRowA> ForwardRows;
    ForwardRows.Add(FName(TEXT("Alpha")), FRepositoryTestRowA{1, 1.0f});
    ForwardRows.Add(FName(TEXT("Beta")), FRepositoryTestRowA{2, 2.0f});
    ForwardRows.Add(FName(TEXT("Gamma")), FRepositoryTestRowA{3, 3.0f});
    TestTrue(TEXT("正序写入成功"), Forward.AddTable(ForwardRows).IsSuccess());

    FStaticDataRepository Reverse;
    RegisterTestRowSerializers(Reverse);
    TMap<FName, FRepositoryTestRowA> ReverseRows;
    ReverseRows.Add(FName(TEXT("Gamma")), FRepositoryTestRowA{3, 3.0f});
    ReverseRows.Add(FName(TEXT("Beta")), FRepositoryTestRowA{2, 2.0f});
    ReverseRows.Add(FName(TEXT("Alpha")), FRepositoryTestRowA{1, 1.0f});
    TestTrue(TEXT("逆序写入成功"), Reverse.AddTable(ReverseRows).IsSuccess());

    FStaticDataRepository Modified;
    RegisterTestRowSerializers(Modified);
    TMap<FName, FRepositoryTestRowA> ModifiedRows;
    ModifiedRows.Add(FName(TEXT("Alpha")), FRepositoryTestRowA{1, 1.0f});
    ModifiedRows.Add(FName(TEXT("Beta")), FRepositoryTestRowA{2, 2.5f});
    ModifiedRows.Add(FName(TEXT("Gamma")), FRepositoryTestRowA{3, 3.0f});
    TestTrue(TEXT("差异样本写入成功"), Modified.AddTable(ModifiedRows).IsSuccess());

    const FStaticDataContentDigest ForwardDigest = Forward.ComputeContentDigest();
    const FStaticDataContentDigest ReverseDigest = Reverse.ComputeContentDigest();
    TestEqual(TEXT("摘要表数一致"), ReverseDigest.TableCount, 1);
    TestEqual(TEXT("摘要行数一致"), ReverseDigest.RowCount, 3);
    TestTrue(TEXT("摘要非空"), !ForwardDigest.Digest.IsEmpty());
    TestEqual(TEXT("摘要与装载顺序无关"), ReverseDigest.Digest, ForwardDigest.Digest);
    TestTrue(TEXT("摘要随数据变化"), Modified.ComputeContentDigest().Digest != ForwardDigest.Digest);

    Repository.Reset();
    TestEqual(TEXT("Reset 后表数归零"), Repository.TableCount(), 0);
    TestTrue(TEXT("Reset 不影响已登记读写器"), Repository.AddTable(Initial).IsSuccess());

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
