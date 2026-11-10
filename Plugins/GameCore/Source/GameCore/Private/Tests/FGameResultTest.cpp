/**
 * @file FGameResultTest.cpp
 * @brief TGameResult 无异常强类型结果模型单元测试
 */

#include "Types/TGameResult.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameResultSuccessAndMoveTest,
    "Game.Core.Result.GameResult.SuccessAndMove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 TGameResult 成功态构造、可变修改、回退取值及仅可移动载荷提取
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameResultSuccessAndMoveTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TGameResult<int32, FString> SuccessResult = TGameResult<int32, FString>::Success(42);
    TestTrue(TEXT("IsSuccess 为真"), SuccessResult.IsSuccess());
    TestFalse(TEXT("IsFailure 为假"), SuccessResult.IsFailure());
    TestTrue(TEXT("显式布尔转换为真"), static_cast<bool>(SuccessResult));
    TestEqual(TEXT("GetValue 等于构造值"), SuccessResult.GetValue(), 42);
    TestEqual(TEXT("成功态 GetValueOr 返回真实值而非回退值"), SuccessResult.GetValueOr(999), 42);

    SuccessResult.GetMutableValue() = 108;
    TestEqual(TEXT("GetMutableValue 原地修改生效"), SuccessResult.GetValue(), 108);

    TGameResult<TUniquePtr<int32>, FString> MoveOnlyResult =
        TGameResult<TUniquePtr<int32>, FString>::Success(MakeUnique<int32>(777));
    TestTrue(TEXT("仅移动类型成功态有效"), MoveOnlyResult.IsSuccess());
    TUniquePtr<int32> ExtractedPtr = MoveOnlyResult.ExtractValue();
    TestNotNull(TEXT("ExtractValue 移出独占指针非空"), ExtractedPtr.Get());
    if (ExtractedPtr.IsValid()) {
        TestEqual(TEXT("ExtractValue 移出数据精确一致"), *ExtractedPtr, 777);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameResultFailureAndHomogeneousTest,
    "Game.Core.Result.GameResult.FailureAndHomogeneousTypes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 TGameResult 失败态行为、错误提取以及相同成功/错误类型实例化无歧义
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameResultFailureAndHomogeneousTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TGameResult<int32, FString> FailedResult = TGameResult<int32, FString>::Failure(TEXT("ERR_TIMEOUT"));
    TestFalse(TEXT("失败态 IsSuccess 为假"), FailedResult.IsSuccess());
    TestTrue(TEXT("失败态 IsFailure 为真"), FailedResult.IsFailure());
    TestFalse(TEXT("失败态显式布尔转换为假"), static_cast<bool>(FailedResult));
    TestEqual(TEXT("失败态 GetValueOr 返回回退值"), FailedResult.GetValueOr(-1), -1);
    TestEqual(TEXT("GetError 返回精确错误信息"), FailedResult.GetError(), FString(TEXT("ERR_TIMEOUT")));

    const FString ExtractedErr = FailedResult.ExtractError();
    TestEqual(TEXT("ExtractError 提取错误信息一致"), ExtractedErr, FString(TEXT("ERR_TIMEOUT")));

    const TGameResult<FString, FString> HomogeneousOk = TGameResult<FString, FString>::Success(TEXT("PayloadText"));
    const TGameResult<FString, FString> HomogeneousErr = TGameResult<FString, FString>::Failure(TEXT("PayloadText"));
    TestTrue(TEXT("同构类型 Success 分支正确识别"), HomogeneousOk.IsSuccess() && !HomogeneousOk.IsFailure());
    TestTrue(TEXT("同构类型 Failure 分支正确识别"), HomogeneousErr.IsFailure() && !HomogeneousErr.IsSuccess());
    TestEqual(TEXT("同构类型成功载荷读取无误"), HomogeneousOk.GetValue(), FString(TEXT("PayloadText")));
    TestEqual(TEXT("同构类型失败载荷读取无误"), HomogeneousErr.GetError(), FString(TEXT("PayloadText")));

    struct FNonDefaultConstructibleError {
        int32 Code;
        explicit FNonDefaultConstructibleError(int32 InCode)
            : Code(InCode) {
        }
    };
    const TGameResult<void, FNonDefaultConstructibleError> VoidOk =
        TGameResult<void, FNonDefaultConstructibleError>::Success();
    TestTrue(TEXT("TGameResult<void> 支持无默认构造函数的 ErrorType 且成功态零开销"), VoidOk.IsSuccess());

    const TGameResult<void, FNonDefaultConstructibleError> VoidErr =
        TGameResult<void, FNonDefaultConstructibleError>::Failure(FNonDefaultConstructibleError(503));
    TestTrue(TEXT("TGameResult<void> 失败态正确保存无默认构造错误对象"), VoidErr.IsFailure());
    TestEqual(TEXT("TGameResult<void> 提取无默认构造错误码一致"), VoidErr.GetError().Code, 503);

    auto InnerFailInt = []() -> TGameStatusResult<int32> {
        return TGameStatusResult<int32>::Failure(404, TEXT("ResourceMissing"));
    };
    auto OuterPropagateToString = [&InnerFailInt]() -> TGameStatusResult<FString> {
        TGameStatusResult<int32> LValueResult = InnerFailInt();
        GAME_TRY_CTX(LValueResult, TEXT("OuterStringStage"));
        return TGameStatusResult<FString>::Success(TEXT("Unreachable"));
    };
    auto TopPropagateToVoid = [&OuterPropagateToString]() -> TGameStatusResult<void> {
        GAME_TRY_CTX(OuterPropagateToString(), TEXT("TopVoidStage"));
        return TGameStatusResult<void>::Success();
    };

    const TGameStatusResult<void> PropagatedVoid = TopPropagateToVoid();
    TestTrue(TEXT("GAME_TRY_CTX 支持左值与跨不同 TSuccess/void 返回类型链式传播失败"), PropagatedVoid.IsFailure());
    TestEqual(TEXT("跨类型传播后错误码保持一致"), PropagatedVoid.GetError().ErrorCode, 404);
    TestTrue(
        TEXT("跨类型传播后错误消息包含两层调用栈上下文"),
        PropagatedVoid.GetError().Message.Contains(TEXT("OuterStringStage")) &&
            PropagatedVoid.GetError().Message.Contains(TEXT("TopVoidStage"))
    );

    return true;
}

#endif
