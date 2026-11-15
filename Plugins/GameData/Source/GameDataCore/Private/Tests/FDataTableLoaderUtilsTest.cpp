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

    const FString MissingPath = TestDirectory / TEXT("Missing.json");
    auto MissingRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(MissingPath);
    TestTrue(TEXT("缺失文件加载失败"), MissingRes.IsFailure());
    TestTrue(TEXT("缺失文件中文提示一致"), MissingRes.GetError().Message.StartsWith(TEXT("文件未找到")));

    const FString MalformedPath = TestDirectory / TEXT("Malformed.json");
    TestTrue(TEXT("写入损坏 JSON 成功"), FFileHelper::SaveStringToFile(TEXT("{ not valid json"), *MalformedPath));
    auto MalformedRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(MalformedPath);
    TestTrue(TEXT("损坏 JSON 加载失败"), MalformedRes.IsFailure());
    TestTrue(TEXT("损坏 JSON 中文提示一致"), MalformedRes.GetError().Message.StartsWith(TEXT("JSON 格式解析失败")));

    const FString ValidPath = TestDirectory / TEXT("Valid.json");
    const FString ValidJson = TEXT("[{\"RowName\":\"Alpha\",\"Score\":42,\"Weight\":1.5}]");
    TestTrue(TEXT("写入有效 JSON 成功"), FFileHelper::SaveStringToFile(ValidJson, *ValidPath));
    auto ValidRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(ValidPath);
    TestTrue(TEXT("有效 JSON 加载成功"), ValidRes.IsSuccess());
    if (ValidRes.IsSuccess()) {
        const auto& Map = ValidRes.GetValue();
        TestEqual(TEXT("有效 JSON 行数"), Map.Num(), 1);
        const FRepositoryTestRowA* LoadedRow = Map.Find(FName(TEXT("Alpha")));
        if (TestNotNull(TEXT("有效 JSON 行键正确"), LoadedRow)) {
            TestEqual(TEXT("有效 JSON 行字段正确"), LoadedRow->Score, 42);
        }
    }

    const FString EmptyJsonPath = TestDirectory / TEXT("Empty.json");
    TestTrue(TEXT("写入空表 JSON 成功"), FFileHelper::SaveStringToFile(TEXT("[]"), *EmptyJsonPath));
    auto EmptyRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(EmptyJsonPath);
    TestTrue(TEXT("空表 JSON 加载成功"), EmptyRes.IsSuccess());
    if (EmptyRes.IsSuccess()) {
        TestEqual(TEXT("空表加载后元素为 0"), EmptyRes.GetValue().Num(), 0);
    }

    const FString NameKeyPath = TestDirectory / TEXT("NameKey.json");
    const FString NameKeyJson = TEXT("[{\"Name\":\"Beta\",\"Score\":88,\"Weight\":2.0}]");
    TestTrue(TEXT("写入 Name 主键 JSON 成功"), FFileHelper::SaveStringToFile(NameKeyJson, *NameKeyPath));
    auto NameKeyRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(NameKeyPath);
    TestTrue(TEXT("Name 主键 JSON 加载成功"), NameKeyRes.IsSuccess());
    if (NameKeyRes.IsSuccess()) {
        const auto& Map = NameKeyRes.GetValue();
        TestEqual(TEXT("Name 主键行数"), Map.Num(), 1);
        const FRepositoryTestRowA* BetaRow = Map.Find(FName(TEXT("Beta")));
        if (TestNotNull(TEXT("Name 主键正确映射"), BetaRow)) {
            TestEqual(TEXT("Name 主键行字段正确"), BetaRow->Score, 88);
        }
    }

    const FString MissingKeyPath = TestDirectory / TEXT("MissingKey.json");
    TestTrue(TEXT("写入缺少主键 JSON 成功"), FFileHelper::SaveStringToFile(TEXT("[{\"Score\":55}]"), *MissingKeyPath));
    AddExpectedErrorPlain(TEXT("缺少有效的 RowName/Name 主键字段"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("加载存在异常"), EAutomationExpectedErrorFlags::Contains, 1);
    auto MissingKeyRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(MissingKeyPath);
    TestTrue(TEXT("缺少主键 JSON 加载失败"), MissingKeyRes.IsFailure());

    const FString DupKeyPath = TestDirectory / TEXT("DupKey.json");
    const FString DupKeyJson = TEXT("[{\"RowName\":\"Dup\",\"Score\":1},{\"RowName\":\"Dup\",\"Score\":2}]");
    TestTrue(TEXT("写入重复主键 JSON 成功"), FFileHelper::SaveStringToFile(DupKeyJson, *DupKeyPath));
    AddExpectedErrorPlain(TEXT("重复出现"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("加载存在异常"), EAutomationExpectedErrorFlags::Contains, 1);
    auto DupKeyRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(DupKeyPath);
    TestTrue(TEXT("重复主键 JSON 加载失败"), DupKeyRes.IsFailure());

    const FString NonObjectPath = TestDirectory / TEXT("NonObject.json");
    TestTrue(TEXT("写入非对象元素 JSON 成功"), FFileHelper::SaveStringToFile(TEXT("[123]"), *NonObjectPath));
    AddExpectedErrorPlain(TEXT("非有效 JSON 对象"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("加载存在异常"), EAutomationExpectedErrorFlags::Contains, 1);
    auto NonObjectRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(NonObjectPath);
    TestTrue(TEXT("非对象元素 JSON 加载失败"), NonObjectRes.IsFailure());

    FileManager.DeleteDirectory(*TestDirectory, false, true);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
