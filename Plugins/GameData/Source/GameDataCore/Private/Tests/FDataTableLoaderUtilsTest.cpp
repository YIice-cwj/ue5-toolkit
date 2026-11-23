/**
 * @file FDataTableLoaderUtilsTest.cpp
 * @brief 数据表 JSON 加载工具类自动化测试（主键兼容、空表、类型错误与统一 DataErrorCodes 契约）
 */

#include "Utils/DataTableLoaderUtils.h"

#if WITH_AUTOMATION_TESTS

#include "Core/DataErrorCodes.h"
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
 * @brief 验证 JSON 加载工具的强类型错误码、多主键识别、非法行拦截与合法空表支持
 * @param Parameters 自动化测试参数
 * @return 测试框架入口固定返回 true
 */
bool FDataTableLoaderUtilsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString TestDirectory = FPaths::ProjectSavedDir() / TEXT("UnitTest_DataTableLoaderUtils");
    IFileManager& FileManager = IFileManager::Get();
    FileManager.DeleteDirectory(*TestDirectory, false, true);
    TestTrue(TEXT("创建 JSON 测试目录成功"), FileManager.MakeDirectory(*TestDirectory, true));

    const FString MissingPath = TestDirectory / TEXT("Missing.json");
    const auto MissingRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(MissingPath);
    if (TestTrue(TEXT("缺失文件加载失败"), MissingRes.IsFailure())) {
        TestEqual(TEXT("缺失文件模块归属为 Data"), MissingRes.GetError().ModuleId, Game::ModuleId::Data);
        TestEqual(TEXT("缺失文件错误码为 JsonFileNotFound"), MissingRes.GetError().ErrorCode, Game::DataErrorCodes::JsonFileNotFound);
    }

    const FString MalformedPath = TestDirectory / TEXT("Malformed.json");
    TestTrue(TEXT("写入损坏 JSON 成功"), FFileHelper::SaveStringToFile(TEXT("{ not valid json"), *MalformedPath));
    const auto MalformedRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(MalformedPath);
    if (TestTrue(TEXT("损坏 JSON 加载失败"), MalformedRes.IsFailure())) {
        TestEqual(TEXT("损坏 JSON 错误码为 JsonParseFailed"), MalformedRes.GetError().ErrorCode, Game::DataErrorCodes::JsonParseFailed);
    }

    const FString ObjectRootPath = TestDirectory / TEXT("ObjectRoot.json");
    TestTrue(TEXT("写入非数组根 JSON 成功"), FFileHelper::SaveStringToFile(TEXT("{\"RowName\":\"Alpha\"}"), *ObjectRootPath));
    const auto ObjectRootRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(ObjectRootPath);
    if (TestTrue(TEXT("非数组根 JSON 加载失败"), ObjectRootRes.IsFailure())) {
        TestEqual(TEXT("非数组根 JSON 错误码为 JsonParseFailed"), ObjectRootRes.GetError().ErrorCode, Game::DataErrorCodes::JsonParseFailed);
    }

    const FString ValidPath = TestDirectory / TEXT("Valid.json");
    const FString ValidJson = TEXT("[{\"RowName\":\"Alpha\",\"Score\":42,\"Weight\":1.5}]");
    TestTrue(TEXT("写入有效 JSON 成功"), FFileHelper::SaveStringToFile(ValidJson, *ValidPath));
    const auto ValidRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(ValidPath);
    TestTrue(TEXT("有效 JSON 加载成功"), ValidRes.IsSuccess());
    if (ValidRes.IsSuccess()) {
        const auto& Map = ValidRes.GetValue();
        TestEqual(TEXT("有效 JSON 行数"), Map.Num(), 1);
        const FRepositoryTestRowA* LoadedRow = Map.Find(FName(TEXT("Alpha")));
        if (TestNotNull(TEXT("有效 JSON 行键正确"), LoadedRow)) {
            TestEqual(TEXT("有效 JSON 行字段正确"), LoadedRow->Score, 42);
            TestEqual(TEXT("有效 JSON 浮点字段正确"), LoadedRow->Weight, 1.5f);
        }
    }

    const FString EmptyJsonPath = TestDirectory / TEXT("Empty.json");
    TestTrue(TEXT("写入空表 JSON 成功"), FFileHelper::SaveStringToFile(TEXT("[]"), *EmptyJsonPath));
    const auto EmptyRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(EmptyJsonPath);
    TestTrue(TEXT("空表 JSON 加载成功"), EmptyRes.IsSuccess());
    if (EmptyRes.IsSuccess()) {
        TestEqual(TEXT("空表加载后元素为 0"), EmptyRes.GetValue().Num(), 0);
    }

    const FString NameKeyPath = TestDirectory / TEXT("NameKey.json");
    const FString NameKeyJson = TEXT("[{\"Name\":\"Beta\",\"Score\":88,\"Weight\":2.0}]");
    TestTrue(TEXT("写入 Name 主键 JSON 成功"), FFileHelper::SaveStringToFile(NameKeyJson, *NameKeyPath));
    const auto NameKeyRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(NameKeyPath);
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
    const auto MissingKeyRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(MissingKeyPath);
    if (TestTrue(TEXT("缺少主键 JSON 加载失败"), MissingKeyRes.IsFailure())) {
        TestEqual(TEXT("缺少主键错误码为 JsonRowValidationFailed"), MissingKeyRes.GetError().ErrorCode, Game::DataErrorCodes::JsonRowValidationFailed);
    }

    const FString DupKeyPath = TestDirectory / TEXT("DupKey.json");
    const FString DupKeyJson = TEXT("[{\"RowName\":\"Dup\",\"Score\":1},{\"RowName\":\"Dup\",\"Score\":2}]");
    TestTrue(TEXT("写入重复主键 JSON 成功"), FFileHelper::SaveStringToFile(DupKeyJson, *DupKeyPath));
    AddExpectedErrorPlain(TEXT("重复出现"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("加载存在异常"), EAutomationExpectedErrorFlags::Contains, 1);
    const auto DupKeyRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(DupKeyPath);
    if (TestTrue(TEXT("重复主键 JSON 加载失败"), DupKeyRes.IsFailure())) {
        TestEqual(TEXT("重复主键错误码为 JsonRowValidationFailed"), DupKeyRes.GetError().ErrorCode, Game::DataErrorCodes::JsonRowValidationFailed);
    }

    const FString NonObjectPath = TestDirectory / TEXT("NonObject.json");
    TestTrue(TEXT("写入非对象元素 JSON 成功"), FFileHelper::SaveStringToFile(TEXT("[123]"), *NonObjectPath));
    AddExpectedErrorPlain(TEXT("非有效 JSON 对象"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("加载存在异常"), EAutomationExpectedErrorFlags::Contains, 1);
    const auto NonObjectRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowA>(NonObjectPath);
    if (TestTrue(TEXT("非对象元素 JSON 加载失败"), NonObjectRes.IsFailure())) {
        TestEqual(TEXT("非对象元素错误码为 JsonRowValidationFailed"), NonObjectRes.GetError().ErrorCode, Game::DataErrorCodes::JsonRowValidationFailed);
    }

    const FString TypeMismatchPath = TestDirectory / TEXT("TypeMismatch.json");
    TestTrue(TEXT("写入字段类型不匹配 JSON 成功"),
        FFileHelper::SaveStringToFile(TEXT("[{\"RowName\":\"BadType\",\"Values\":\"not_an_array\"}]"), *TypeMismatchPath));
    AddExpectedErrorPlain(TEXT("Expecting JSON array"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("Unable to import JSON value into property"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("反序列化失败"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedErrorPlain(TEXT("加载存在异常"), EAutomationExpectedErrorFlags::Contains, 1);
    const auto TypeMismatchRes = FDataTableLoaderUtils::LoadTableFromJson<FRepositoryTestRowB>(TypeMismatchPath);
    if (TestTrue(TEXT("字段类型不匹配 JSON 加载失败"), TypeMismatchRes.IsFailure())) {
        TestEqual(TEXT("字段类型不匹配错误码为 JsonRowValidationFailed"), TypeMismatchRes.GetError().ErrorCode, Game::DataErrorCodes::JsonRowValidationFailed);
    }

    FileManager.DeleteDirectory(*TestDirectory, false, true);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
