/**
 * @file FDataTableLoaderUtilsTest.cpp
 * @brief 数据表 JSON 加载工具类自动化测试（主键兼容、空表、异常截断与原子回滚）
 */

#include "Utils/DataTableLoaderUtils.h"

#if WITH_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/FStaticDataRepositoryTestRows.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDataTableLoaderUtilsTest,
    "Game.Data.DataTableLoaderUtils",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 JSON 加载工具的前置异常清空、多主键识别与合法空表支持
 * @param Parameters 自动化测试参数
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

    const FString EmptyJsonPath = TestDirectory / TEXT("Empty.json");
    TestTrue(TEXT("写入空表 JSON 成功"), FFileHelper::SaveStringToFile(TEXT("[]"), *EmptyJsonPath));
    TestTrue(TEXT("空表 JSON 加载成功"), FDataTableLoaderUtils::LoadTableFromJson(EmptyJsonPath, OutRows));
    TestEqual(TEXT("空表加载后元素为 0"), OutRows.Num(), 0);

    const FString NameKeyPath = TestDirectory / TEXT("NameKey.json");
    const FString NameKeyJson = TEXT("[{\"Name\":\"Beta\",\"Score\":88,\"Weight\":2.0}]");
    TestTrue(TEXT("写入 Name 主键 JSON 成功"), FFileHelper::SaveStringToFile(NameKeyJson, *NameKeyPath));
    TestTrue(TEXT("Name 主键 JSON 加载成功"), FDataTableLoaderUtils::LoadTableFromJson(NameKeyPath, OutRows));
    TestEqual(TEXT("Name 主键行数"), OutRows.Num(), 1);
    const FRepositoryTestRowA* BetaRow = OutRows.Find(FName(TEXT("Beta")));
    if (TestNotNull(TEXT("Name 主键正确映射"), BetaRow)) {
        TestEqual(TEXT("Name 主键行字段正确"), BetaRow->Score, 88);
    }

    const FString MissingKeyPath = TestDirectory / TEXT("MissingKey.json");
    TestTrue(TEXT("写入缺少主键 JSON 成功"), FFileHelper::SaveStringToFile(TEXT("[{\"Score\":55}]"), *MissingKeyPath));
    AddExpectedErrorPlain(TEXT("缺少有效的 RowName/Name 主键字段"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("加载存在异常"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("缺少主键 JSON 加载失败"), FDataTableLoaderUtils::LoadTableFromJson(MissingKeyPath, OutRows));
    TestEqual(TEXT("缺少主键加载后输出清空"), OutRows.Num(), 0);

    const FString DupKeyPath = TestDirectory / TEXT("DupKey.json");
    const FString DupKeyJson = TEXT("[{\"RowName\":\"Dup\",\"Score\":1},{\"RowName\":\"Dup\",\"Score\":2}]");
    TestTrue(TEXT("写入重复主键 JSON 成功"), FFileHelper::SaveStringToFile(DupKeyJson, *DupKeyPath));
    AddExpectedErrorPlain(TEXT("重复出现"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("加载存在异常"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("重复主键 JSON 加载失败"), FDataTableLoaderUtils::LoadTableFromJson(DupKeyPath, OutRows));
    TestEqual(TEXT("重复主键加载后输出清空"), OutRows.Num(), 0);

    const FString NonObjectPath = TestDirectory / TEXT("NonObject.json");
    TestTrue(TEXT("写入非对象元素 JSON 成功"), FFileHelper::SaveStringToFile(TEXT("[123]"), *NonObjectPath));
    AddExpectedErrorPlain(TEXT("非有效 JSON 对象"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("加载存在异常"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("非对象元素 JSON 加载失败"), FDataTableLoaderUtils::LoadTableFromJson(NonObjectPath, OutRows));
    TestEqual(TEXT("非对象元素加载后输出清空"), OutRows.Num(), 0);

    FileManager.DeleteDirectory(*TestDirectory, false, true);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
