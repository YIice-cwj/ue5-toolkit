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

    // 默认模板参数 TError = FGameError 契约验证
    TGameResult<int32> DefaultErrorResult = TGameResult<int32>::Failure(FGameError(TEXT("DefaultErrorMsg")));
    static_assert(std::is_same_v<typename decltype(DefaultErrorResult)::ErrorType, FGameError>, "TGameResult 默认错误载荷必须为 FGameError");
    TestTrue(TEXT("单实参模板 TGameResult<int32> 默认采用 FGameError"), DefaultErrorResult.IsFailure());
    TestEqual(TEXT("FGameError 字符串构造器信息正确"), DefaultErrorResult.GetError().Message, FString(TEXT("DefaultErrorMsg")));
    TestEqual(TEXT("默认模块 ID 为 Core"), DefaultErrorResult.GetError().ModuleId, Game::ModuleId::Core);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameResultMonadicOperatorsTest,
    "Game.Core.Result.GameResult.MonadicOperators",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 TGameResult Monadic 链式算子 (AndThen, Map, MapError, OrElse) 及仅移动右值支持
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameResultMonadicOperatorsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    // 1. Map 测试
    const TGameStatusResult<int32> InitialOk = TGameStatusResult<int32>::Success(21);
    const TGameStatusResult<int32> MappedOk = InitialOk.Map([](int32 V) { return V * 2; });
    TestTrue(TEXT("Map 变换后保持成功态"), MappedOk.IsSuccess());
    TestEqual(TEXT("Map 变换后值翻倍为 42"), MappedOk.GetValue(), 42);

    const TGameStatusResult<void> MappedToVoid = InitialOk.Map([](int32) {});
    TestTrue(TEXT("Map 映射为 void 返回成功态 TGameResult<void>"), MappedToVoid.IsSuccess());

    const TGameStatusResult<int32> InitialFail = TGameStatusResult<int32>::Failure(500, TEXT("InternalErr"));
    bool bMapInvokedOnFail = false;
    const TGameStatusResult<int32> MappedFail = InitialFail.Map([&bMapInvokedOnFail](int32 V) {
        bMapInvokedOnFail = true;
        return V * 2;
    });
    TestFalse(TEXT("失败态 Map 不执行闭包"), bMapInvokedOnFail);
    TestTrue(TEXT("失败态 Map 保持失败"), MappedFail.IsFailure());
    TestEqual(TEXT("失败态 Map 错误码保持不变"), MappedFail.GetError().ErrorCode, 500);

    // 2. AndThen 链式测试
    const TGameStatusResult<FString> ChainedOk = TGameStatusResult<int32>::Success(100)
        .AndThen([](int32 V) -> TGameStatusResult<float> {
            return TGameStatusResult<float>::Success(static_cast<float>(V) * 0.5f);
        })
        .AndThen([](float V) -> TGameStatusResult<FString> {
            return TGameStatusResult<FString>::Success(FString::Printf(TEXT("Val=%.1f"), V));
        });
    TestTrue(TEXT("AndThen 连续两级链式调用成功"), ChainedOk.IsSuccess());
    TestEqual(TEXT("AndThen 最终计算结果符合预期"), ChainedOk.GetValue(), FString(TEXT("Val=50.0")));

    const TGameStatusResult<FString> ShortCircuited = TGameStatusResult<int32>::Success(100)
        .AndThen([](int32) -> TGameStatusResult<float> {
            return TGameStatusResult<float>::Failure(403, TEXT("ForbiddenMiddle"));
        })
        .AndThen([](float) -> TGameStatusResult<FString> {
            return TGameStatusResult<FString>::Success(TEXT("ShouldNotReach"));
        });
    TestTrue(TEXT("中间步骤失败时 AndThen 链路自动短路"), ShortCircuited.IsFailure());
    TestEqual(TEXT("短路后错误码来自中间失败步骤"), ShortCircuited.GetError().ErrorCode, 403);
    TestEqual(TEXT("短路后错误消息来自中间步骤"), ShortCircuited.GetError().Message, FString(TEXT("ForbiddenMiddle")));

    // 3. MapError 错误转换测试
    const TGameResult<int32, FString> CustomErrResult = InitialFail.MapError([](const FGameError& Err) {
        return FString::Printf(TEXT("Mapped[%d]:%s"), Err.ErrorCode, *Err.Message);
    });
    TestTrue(TEXT("MapError 转换后为失败态"), CustomErrResult.IsFailure());
    TestEqual(TEXT("MapError 生成新错误格式"), CustomErrResult.GetError(), FString(TEXT("Mapped[500]:InternalErr")));

    // 4. OrElse 兜底恢复测试
    const TGameStatusResult<int32> Recovered = InitialFail.OrElse([](const FGameError& Err) {
        if (Err.ErrorCode == 500) {
            return TGameStatusResult<int32>::Success(999);
        }
        return TGameStatusResult<int32>::Failure(Err);
    });
    TestTrue(TEXT("OrElse 成功将 500 错误降级转正为成功态"), Recovered.IsSuccess());
    TestEqual(TEXT("OrElse 恢复后的兜底数值正确"), Recovered.GetValue(), 999);

    const TGameStatusResult<int32> UnneededRecovery = InitialOk.OrElse([](const FGameError&) {
        return TGameStatusResult<int32>::Success(0);
    });
    TestEqual(TEXT("成功态调用 OrElse 原样保留"), UnneededRecovery.GetValue(), 21);

    // 5. TGameResult<void> 的 Monadic 算子测试
    const TGameStatusResult<void> VoidStart = TGameStatusResult<void>::Success();
    const TGameStatusResult<int32> VoidToVal = VoidStart.Map([]() { return 123; });
    TestTrue(TEXT("void.Map 返回成功态"), VoidToVal.IsSuccess());
    TestEqual(TEXT("void.Map 生成新值"), VoidToVal.GetValue(), 123);

    const TGameStatusResult<int32> VoidAndThen = VoidStart.AndThen([]() -> TGameStatusResult<int32> {
        return TGameStatusResult<int32>::Success(456);
    });
    TestTrue(TEXT("void.AndThen 成功"), VoidAndThen.IsSuccess());
    TestEqual(TEXT("void.AndThen 生成新值"), VoidAndThen.GetValue(), 456);

    const TGameStatusResult<void> VoidFail = TGameStatusResult<void>::Failure(400, TEXT("VoidFail"));
    const TGameStatusResult<void> VoidRecovered = VoidFail.OrElse([](const FGameError&) {
        return TGameStatusResult<void>::Success();
    });
    TestTrue(TEXT("void.OrElse 成功恢复"), VoidRecovered.IsSuccess());

    // 6. 仅移动类型（Move-Only）右值链路零拷贝验证
    TGameResult<TUniquePtr<int32>, FString> MoveOnlyStart =
        TGameResult<TUniquePtr<int32>, FString>::Success(MakeUnique<int32>(888));
    TGameResult<int32, FString> MoveOnlyFinal = MoveTemp(MoveOnlyStart)
        .Map([](TUniquePtr<int32> Ptr) {
            return *Ptr + 12;
        });
    TestTrue(TEXT("仅移动类型通过右值重载成功完成零拷贝 Map 链式流转"), MoveOnlyFinal.IsSuccess());
    TestEqual(TEXT("仅移动类型解引用计算值精确匹配 (888+12=900)"), MoveOnlyFinal.GetValue(), 900);

    return true;
}

#endif
