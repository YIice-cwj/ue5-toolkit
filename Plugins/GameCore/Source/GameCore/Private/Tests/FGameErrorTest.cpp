/**
 * @file FGameErrorTest.cpp
 * @brief FGameError 与 TGameStatusResult 单元测试（错误契约、严重性判定与强类型状态流转）
 */

#include "Types/FGameError.h"
#include "Types/TGameResult.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameErrorContractAndSeverityTest,
    "Game.Core.Result.Error.ContractAndSeverity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameError 工厂构造、状态判定与诊断文案输出
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameErrorContractAndSeverityTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameError SuccessErr = FGameError::Ok();
    TestTrue(TEXT("Ok() 属于成功态"), SuccessErr.IsOk());
    TestFalse(TEXT("Ok() 不属于错误态"), SuccessErr.IsError());
    TestFalse(TEXT("Ok() 不属于致命态"), SuccessErr.IsCritical());
    TestEqual(TEXT("Ok() 错误码为 0"), SuccessErr.ErrorCode, 0);
    TestEqual(TEXT("Ok() 严重级别为 Success"), SuccessErr.Severity, EGameErrorSeverity::Success);
    TestEqual(TEXT("Debug 模块 ID 正确"), Game::ModuleId::Debug, 12);

    const FGameError WarningErr = FGameError::Make(
        Game::ModuleId::Core,
        1001,
        TEXT("MemoryThresholdExceeded"),
        EGameErrorSeverity::Warning
    );
    TestTrue(TEXT("Warning 属于错误态"), WarningErr.IsError());
    TestFalse(TEXT("Warning 不属于成功态"), WarningErr.IsOk());
    TestFalse(TEXT("Warning 不属于致命态"), WarningErr.IsCritical());
    TestEqual(TEXT("模块 ID 正确"), WarningErr.ModuleId, Game::ModuleId::Core);
    TestEqual(TEXT("错误码正确"), WarningErr.ErrorCode, 1001);
    TestEqual(TEXT("错误消息一致"), WarningErr.Message, TEXT("MemoryThresholdExceeded"));

    const FGameError CriticalErr = FGameError::Make(
        Game::ModuleId::Net,
        5000,
        TEXT("SocketDisconnectedFatal"),
        EGameErrorSeverity::Critical
    );
    TestTrue(TEXT("Critical 属于致命态"), CriticalErr.IsCritical());
    TestTrue(TEXT("Critical 属于错误态"), CriticalErr.IsError());

    const FString Formatted = CriticalErr.ToString();
    TestTrue(TEXT("诊断文本包含 Module"), Formatted.Contains(TEXT("Module:2")));
    TestTrue(TEXT("诊断文本包含 Code"), Formatted.Contains(TEXT("Code:5000")));
    TestTrue(TEXT("诊断文本包含 Message"), Formatted.Contains(TEXT("SocketDisconnectedFatal")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameErrorResultIntegrationTest,
    "Game.Core.Result.Error.ResultIntegration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameError 与 TGameStatusResult 强类型结果容器深度集成
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameErrorResultIntegrationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const auto SuccessResult = TGameStatusResult<int32>::Success(1024);
    TestTrue(TEXT("结果容器处于成功态"), SuccessResult.IsSuccess());
    TestFalse(TEXT("结果容器非失败态"), SuccessResult.IsFailure());
    TestEqual(TEXT("成功载荷获取准确"), SuccessResult.GetValue(), 1024);

    const FGameError ExpectedError = FGameError::Make(
        Game::ModuleId::Asset,
        404,
        TEXT("AssetNotFound"),
        EGameErrorSeverity::Error
    );
    const auto FailureResult = TGameStatusResult<int32>::Failure(ExpectedError);
    TestTrue(TEXT("结果容器处于失败态"), FailureResult.IsFailure());
    TestFalse(TEXT("结果容器非成功态"), FailureResult.IsSuccess());
    TestEqual(TEXT("提取错误对象与预期精确一致"), FailureResult.GetError(), ExpectedError);
    TestEqual(TEXT("回退值生效"), FailureResult.GetValueOr(999), 999);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
