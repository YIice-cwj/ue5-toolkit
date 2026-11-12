/**
 * @file TGameResult.h
 * @brief 无异常强类型结果容器模板（成功值或错误载荷内联二选一，支持相同成功/错误类型实例化）
 */

#pragma once

#include "CoreMinimal.h"
#include <type_traits>
#include "Misc/Optional.h"
#include "Misc/TVariant.h"
#include "Types/FGameError.h"

/**
 * @brief 跨成功类型链式错误传播转发器（供 GAME_TRY / GAME_TRY_CTX 隐式转换为外层函数的 TGameResult<AnySuccess, TError>）
 * @tparam TError 错误载荷类型
 */
template <typename TError>
struct TGameErrorForwarder {
    TError Error;

    /**
     * @brief 移动构造错误转发器
     * @param InError 错误载荷
     */
    explicit TGameErrorForwarder(TError InError)
        : Error(MoveTemp(InError)) {
    }
};

/**
 * @brief 无异常强类型结果模型（支持相同成功/错误类型实例化）
 * @tparam TSuccess 成功载荷类型
 * @tparam TError 错误载荷类型（默认统一为标准化 FGameError 契约）
 */
template <typename TSuccess, typename TError = FGameError>
class [[nodiscard]] TGameResult {
public:
    using SuccessType = TSuccess;
    using ErrorType = TError;

private:
    enum class ESuccessTag : uint8 { Tag };
    enum class EErrorTag : uint8 { Tag };

    struct FSuccessStorage {
        SuccessType Value;
    };

    struct FErrorStorage {
        ErrorType Error;
    };

public:
    /**
     * @brief 判定当前结果是否处于成功态
     * @return 成功态返回 true
     */
    [[nodiscard]] bool IsSuccess() const noexcept {
        return Storage.template IsType<FSuccessStorage>();
    }

    /**
     * @brief 判定当前结果是否处于失败态
     * @return 失败态返回 true
     */
    [[nodiscard]] bool IsFailure() const noexcept {
        return Storage.template IsType<FErrorStorage>();
    }

    /**
     * @brief 布尔显式转换操作符
     * @return 成功态返回 true
     */
    [[nodiscard]] explicit operator bool() const noexcept {
        return IsSuccess();
    }

    /**
     * @brief 获取成功载荷只读引用（调用前须处于成功态）
     * @return 成功载荷常量引用
     */
    [[nodiscard]] const SuccessType& GetValue() const;

    /**
     * @brief 解引用运算符重载（只读版本，调用前须处于成功态）
     * @return 成功载荷常量引用
     */
    [[nodiscard]] const SuccessType& operator*() const noexcept {
        return GetValue();
    }

    /**
     * @brief 获取成功载荷可变引用（调用前须处于成功态）
     * @return 成功载荷可变引用
     */
    [[nodiscard]] SuccessType& GetMutableValue();

    /**
     * @brief 解引用运算符重载（可变版本，调用前须处于成功态）
     * @return 成功载荷可变引用
     */
    [[nodiscard]] SuccessType& operator*() noexcept {
        return GetMutableValue();
    }

    /**
     * @brief 成员指针访问运算符重载（只读版本，调用前须处于成功态）
     * @return 成功载荷常量指针
     */
    [[nodiscard]] const SuccessType* operator->() const noexcept {
        return &GetValue();
    }

    /**
     * @brief 成员指针访问运算符重载（可变版本，调用前须处于成功态）
     * @return 成功载荷可变指针
     */
    [[nodiscard]] SuccessType* operator->() noexcept {
        return &GetMutableValue();
    }

    /**
     * @brief 获取成功载荷副本，若处于失败态则返回常量引用回退值副本
     * @param FallbackValue 失败时的回退值常量引用
     * @return 成功载荷或回退值
     */
    [[nodiscard]] SuccessType GetValueOr(const SuccessType& FallbackValue) const &;

    /**
     * @brief 获取成功载荷副本，若处于失败态则移动返回右值回退值
     * @param FallbackValue 失败时的回退值右值引用
     * @return 成功载荷或回退值
     */
    [[nodiscard]] SuccessType GetValueOr(SuccessType&& FallbackValue) const &;

    /**
     * @brief 移动获取成功载荷，若处于失败态则移动返回回退值（支持仅移动载荷类型）
     * @param FallbackValue 失败时的回退值右值引用
     * @return 移动出的成功载荷或回退值
     */
    [[nodiscard]] SuccessType GetValueOr(SuccessType&& FallbackValue) &&;

    /**
     * @brief 移动获取成功载荷，若处于失败态则返回回退值副本
     * @param FallbackValue 失败时的回退值常量引用
     * @return 移动出的成功载荷或回退值
     */
    [[nodiscard]] SuccessType GetValueOr(const SuccessType& FallbackValue) &&;

    /**
     * @brief 移动提取成功载荷（调用前须处于成功态，提取后原载荷处于被移走状态）
     * @return 移动出的成功载荷
     */
    [[nodiscard]] SuccessType ExtractValue();

    /**
     * @brief 获取错误载荷只读引用（调用前须处于失败态）
     * @return 错误载荷常量引用
     */
    [[nodiscard]] const ErrorType& GetError() const;

    /**
     * @brief 移动提取错误载荷（调用前须处于失败态）
     * @return 移动出的错误载荷
     */
    [[nodiscard]] ErrorType ExtractError();

    /**
     * @brief 链式绑定算子（左值版本）：成功时调用返回新 TGameResult 的闭包，失败时短路透传错误
     * @tparam F 接收 const SuccessType& 并返回 TGameResult<NewSuccess, ErrorType> 的闭包
     * @param Func 变换闭包
     * @return 链式变换后的结果实例
     */
    template <typename F>
    auto AndThen(F&& Func) const & {
        using ReturnResultType = std::invoke_result_t<F, const SuccessType&>;
        if (IsSuccess()) {
            return Func(GetValue());
        }
        return ReturnResultType::Failure(GetError());
    }

    /**
     * @brief 链式绑定算子（右值移动版本）：成功时移动载荷并调用返回新 TGameResult 的闭包，失败时移动透传错误
     * @tparam F 接收 SuccessType&& 并返回 TGameResult<NewSuccess, ErrorType> 的闭包
     * @param Func 变换闭包
     * @return 链式变换后的结果实例
     */
    template <typename F>
    auto AndThen(F&& Func) && {
        using ReturnResultType = std::invoke_result_t<F, SuccessType>;
        if (IsSuccess()) {
            return Func(ExtractValue());
        }
        return ReturnResultType::Failure(ExtractError());
    }

    /**
     * @brief 纯值映射算子（左值版本）：成功时将成功载荷映射为新值，失败时短路透传错误
     * @tparam F 接收 const SuccessType& 并返回普通值的闭包
     * @param Func 映射闭包
     * @return 包含映射后新值的结果实例
     */
    template <typename F>
    auto Map(F&& Func) const & {
        using NewSuccessType = std::invoke_result_t<F, const SuccessType&>;
        if constexpr (std::is_void_v<NewSuccessType>) {
            if (IsSuccess()) {
                Func(GetValue());
                return TGameResult<void, ErrorType>::Success();
            }
            return TGameResult<void, ErrorType>::Failure(GetError());
        } else {
            using ReturnResultType = TGameResult<NewSuccessType, ErrorType>;
            if (IsSuccess()) {
                return ReturnResultType::Success(Func(GetValue()));
            }
            return ReturnResultType::Failure(GetError());
        }
    }

    /**
     * @brief 纯值映射算子（右值移动版本）：成功时移动载荷并映射为新值，失败时移动透传错误
     * @tparam F 接收 SuccessType&& 并返回普通值的闭包
     * @param Func 映射闭包
     * @return 包含映射后新值的结果实例
     */
    template <typename F>
    auto Map(F&& Func) && {
        using NewSuccessType = std::invoke_result_t<F, SuccessType>;
        if constexpr (std::is_void_v<NewSuccessType>) {
            if (IsSuccess()) {
                Func(ExtractValue());
                return TGameResult<void, ErrorType>::Success();
            }
            return TGameResult<void, ErrorType>::Failure(ExtractError());
        } else {
            using ReturnResultType = TGameResult<NewSuccessType, ErrorType>;
            if (IsSuccess()) {
                return ReturnResultType::Success(Func(ExtractValue()));
            }
            return ReturnResultType::Failure(ExtractError());
        }
    }

    /**
     * @brief 错误映射算子（左值版本）：失败时将错误载荷转换为新错误类型，成功时原样透传
     * @tparam F 接收 const ErrorType& 并返回新错误对象的闭包
     * @param Func 错误转换闭包
     * @return 携带转换后错误的结果实例
     */
    template <typename F>
    auto MapError(F&& Func) const & {
        using NewErrorType = std::invoke_result_t<F, const ErrorType&>;
        using ReturnResultType = TGameResult<SuccessType, NewErrorType>;
        if (IsSuccess()) {
            return ReturnResultType::Success(GetValue());
        }
        return ReturnResultType::Failure(Func(GetError()));
    }

    /**
     * @brief 错误映射算子（右值移动版本）：失败时移动错误载荷并转换为新错误类型，成功时移动透传
     * @tparam F 接收 ErrorType&& 并返回新错误对象的闭包
     * @param Func 错误转换闭包
     * @return 携带转换后错误的结果实例
     */
    template <typename F>
    auto MapError(F&& Func) && {
        using NewErrorType = std::invoke_result_t<F, ErrorType>;
        using ReturnResultType = TGameResult<SuccessType, NewErrorType>;
        if (IsSuccess()) {
            return ReturnResultType::Success(ExtractValue());
        }
        return ReturnResultType::Failure(Func(ExtractError()));
    }

    /**
     * @brief 错误恢复算子（左值版本）：失败时调用闭包进行降级或兜底恢复，成功时原样返回
     * @tparam F 接收 const ErrorType& 并返回 TGameResult<SuccessType, ErrorType> 的闭包
     * @param RecoveryFunc 错误恢复闭包
     * @return 恢复或原样结果实例
     */
    template <typename F>
    TGameResult OrElse(F&& RecoveryFunc) const & {
        if (IsSuccess()) {
            return *this;
        }
        return RecoveryFunc(GetError());
    }

    /**
     * @brief 错误恢复算子（右值移动版本）：失败时移动错误载荷并调用闭包进行降级或兜底恢复，成功时移动返回
     * @tparam F 接收 ErrorType&& 并返回 TGameResult<SuccessType, ErrorType> 的闭包
     * @param RecoveryFunc 错误恢复闭包
     * @return 恢复或原样结果实例
     */
    template <typename F>
    TGameResult OrElse(F&& RecoveryFunc) && {
        if (IsSuccess()) {
            return MoveTemp(*this);
        }
        return RecoveryFunc(ExtractError());
    }

private:
    TGameResult(ESuccessTag, SuccessType InValue);
    TGameResult(EErrorTag, ErrorType InError);

public:
    /**
     * @brief 从链式错误转发器隐式构造失败结果（支持 GAME_TRY 跨不同成功载荷类型传播错误）
     * @param ForwardedError 错误转发器右值
     */
    TGameResult(TGameErrorForwarder<ErrorType>&& ForwardedError)
        : TGameResult(EErrorTag::Tag, MoveTemp(ForwardedError.Error)) {
    }

    /**
     * @brief 构造携带成功值的结果对象
     * @param InValue 成功载荷
     * @return 成功态结果实例
     */
    [[nodiscard]] static TGameResult Success(SuccessType InValue);

    /**
     * @brief 构造携带错误信息的结果对象
     * @param InError 错误载荷
     * @return 失败态结果实例
     */
    [[nodiscard]] static TGameResult Failure(ErrorType InError);

    /**
     * @brief 快速构造结构化 FGameError 失败结果（错误码 + 语义描述）
     * @tparam E 错误类型推导占位
     * @param InErrorCode 模块内错误码
     * @param InMessage 语义错误说明
     * @return 失败态结果实例
     */
    template <typename E = TError, typename = std::enable_if_t<std::is_same_v<E, FGameError>>>
    [[nodiscard]] static TGameResult Failure(int32 InErrorCode, FString InMessage) {
        return Failure(FGameError(Game::ModuleId::Core, InErrorCode, EGameErrorSeverity::Error, MoveTemp(InMessage)));
    }

    /**
     * @brief 快速构造结构化 FGameError 失败结果（模块 ID + 错误码 + 语义描述）
     * @tparam E 错误类型推导占位
     * @param InModuleId 系统模块 ID
     * @param InErrorCode 模块内错误码
     * @param InMessage 语义错误说明
     * @return 失败态结果实例
     */
    template <typename E = TError, typename = std::enable_if_t<std::is_same_v<E, FGameError>>>
    [[nodiscard]] static TGameResult Failure(int32 InModuleId, int32 InErrorCode, FString InMessage) {
        return Failure(FGameError(InModuleId, InErrorCode, EGameErrorSeverity::Error, MoveTemp(InMessage)));
    }

    /**
     * @brief 快速构造结构化 FGameError 失败结果（模块 ID + 错误码 + 严重级别 + 语义描述）
     * @tparam E 错误类型推导占位
     * @param InModuleId 系统模块 ID
     * @param InErrorCode 模块内错误码
     * @param InSeverity 错误严重级别
     * @param InMessage 语义错误说明
     * @return 失败态结果实例
     */
    template <typename E = TError, typename = std::enable_if_t<std::is_same_v<E, FGameError>>>
    [[nodiscard]] static TGameResult Failure(
        int32 InModuleId,
        int32 InErrorCode,
        EGameErrorSeverity InSeverity,
        FString InMessage
    ) {
        return Failure(FGameError(InModuleId, InErrorCode, InSeverity, MoveTemp(InMessage)));
    }

private:
    TVariant<FSuccessStorage, FErrorStorage> Storage;
};

template <typename TSuccess, typename TError>
TGameResult<TSuccess, TError>::TGameResult(ESuccessTag, SuccessType InValue)
    : Storage(TInPlaceType<FSuccessStorage>(), FSuccessStorage{MoveTemp(InValue)}) {
}

template <typename TSuccess, typename TError>
TGameResult<TSuccess, TError>::TGameResult(EErrorTag, ErrorType InError)
    : Storage(TInPlaceType<FErrorStorage>(), FErrorStorage{MoveTemp(InError)}) {
}

template <typename TSuccess, typename TError>
const typename TGameResult<TSuccess, TError>::SuccessType& TGameResult<TSuccess, TError>::GetValue() const {
    checkf(IsSuccess(), TEXT("Attempted GetValue() on a failed TGameResult"));
    return Storage.template Get<FSuccessStorage>().Value;
}

template <typename TSuccess, typename TError>
typename TGameResult<TSuccess, TError>::SuccessType& TGameResult<TSuccess, TError>::GetMutableValue() {
    checkf(IsSuccess(), TEXT("Attempted GetMutableValue() on a failed TGameResult"));
    return Storage.template Get<FSuccessStorage>().Value;
}

template <typename TSuccess, typename TError>
typename TGameResult<TSuccess, TError>::SuccessType TGameResult<TSuccess, TError>::GetValueOr(
    const SuccessType& FallbackValue
) const & {
    if (IsSuccess()) {
        return Storage.template Get<FSuccessStorage>().Value;
    }
    return FallbackValue;
}

template <typename TSuccess, typename TError>
typename TGameResult<TSuccess, TError>::SuccessType TGameResult<TSuccess, TError>::GetValueOr(
    SuccessType&& FallbackValue
) const & {
    if (IsSuccess()) {
        return Storage.template Get<FSuccessStorage>().Value;
    }
    return MoveTemp(FallbackValue);
}

template <typename TSuccess, typename TError>
typename TGameResult<TSuccess, TError>::SuccessType TGameResult<TSuccess, TError>::GetValueOr(
    SuccessType&& FallbackValue
) && {
    if (IsSuccess()) {
        return ExtractValue();
    }
    return MoveTemp(FallbackValue);
}

template <typename TSuccess, typename TError>
typename TGameResult<TSuccess, TError>::SuccessType TGameResult<TSuccess, TError>::GetValueOr(
    const SuccessType& FallbackValue
) && {
    if (IsSuccess()) {
        return ExtractValue();
    }
    return FallbackValue;
}

template <typename TSuccess, typename TError>
typename TGameResult<TSuccess, TError>::SuccessType TGameResult<TSuccess, TError>::ExtractValue() {
    checkf(IsSuccess(), TEXT("Attempted ExtractValue() on a failed TGameResult"));
    return MoveTemp(Storage.template Get<FSuccessStorage>().Value);
}

template <typename TSuccess, typename TError>
const typename TGameResult<TSuccess, TError>::ErrorType& TGameResult<TSuccess, TError>::GetError() const {
    checkf(IsFailure(), TEXT("Attempted GetError() on a successful TGameResult"));
    return Storage.template Get<FErrorStorage>().Error;
}

template <typename TSuccess, typename TError>
typename TGameResult<TSuccess, TError>::ErrorType TGameResult<TSuccess, TError>::ExtractError() {
    checkf(IsFailure(), TEXT("Attempted ExtractError() on a successful TGameResult"));
    return MoveTemp(Storage.template Get<FErrorStorage>().Error);
}

template <typename TSuccess, typename TError>
TGameResult<TSuccess, TError> TGameResult<TSuccess, TError>::Success(SuccessType InValue) {
    return TGameResult(ESuccessTag::Tag, MoveTemp(InValue));
}

template <typename TSuccess, typename TError>
TGameResult<TSuccess, TError> TGameResult<TSuccess, TError>::Failure(ErrorType InError) {
    return TGameResult(EErrorTag::Tag, MoveTemp(InError));
}

/**
 * @brief 无返回值 (void) 强类型结果特化（使用 TOptional<ErrorType> 避免成功态默认构造错误对象）
 * @tparam TError 错误载荷类型
 */
template <typename TError>
class [[nodiscard]] TGameResult<void, TError> {
public:
    using SuccessType = void;
    using ErrorType = TError;

public:
    /**
     * @brief 从链式错误转发器隐式构造无返回值失败结果（支持 GAME_TRY 跨不同成功载荷类型传播错误）
     * @param ForwardedError 错误转发器右值
     */
    TGameResult(TGameErrorForwarder<ErrorType>&& ForwardedError)
        : Error(MoveTemp(ForwardedError.Error)) {
    }

    /**
     * @brief 判定当前结果是否处于成功态
     * @return 成功态返回 true
     */
    [[nodiscard]] bool IsSuccess() const noexcept {
        return !Error.IsSet();
    }

    /**
     * @brief 判定当前结果是否处于失败态
     * @return 失败态返回 true
     */
    [[nodiscard]] bool IsFailure() const noexcept {
        return Error.IsSet();
    }

    /**
     * @brief 布尔显式转换操作符
     * @return 成功态返回 true
     */
    [[nodiscard]] explicit operator bool() const noexcept {
        return IsSuccess();
    }

    /**
     * @brief 获取错误载荷只读引用（调用前须处于失败态）
     * @return 错误载荷常量引用
     */
    [[nodiscard]] const ErrorType& GetError() const {
        checkf(IsFailure(), TEXT("Attempted GetError() on a successful TGameResult<void>"));
        return Error.GetValue();
    }

    /**
     * @brief 移动提取错误载荷（调用前须处于失败态）
     * @return 移动出的错误载荷
     */
    [[nodiscard]] ErrorType ExtractError() {
        checkf(IsFailure(), TEXT("Attempted ExtractError() on a successful TGameResult<void>"));
        return MoveTemp(Error.GetValue());
    }

    /**
     * @brief 链式绑定算子（左值版本）：成功时调用返回新 TGameResult 的无参闭包，失败时短路透传错误
     * @tparam F 返回 TGameResult<NewSuccess, ErrorType> 的无参闭包
     * @param Func 变换闭包
     * @return 链式变换后的结果实例
     */
    template <typename F>
    auto AndThen(F&& Func) const & {
        using ReturnResultType = std::invoke_result_t<F>;
        if (IsSuccess()) {
            return Func();
        }
        return ReturnResultType::Failure(GetError());
    }

    /**
     * @brief 链式绑定算子（右值移动版本）：成功时调用返回新 TGameResult 的无参闭包，失败时移动透传错误
     * @tparam F 返回 TGameResult<NewSuccess, ErrorType> 的无参闭包
     * @param Func 变换闭包
     * @return 链式变换后的结果实例
     */
    template <typename F>
    auto AndThen(F&& Func) && {
        using ReturnResultType = std::invoke_result_t<F>;
        if (IsSuccess()) {
            return Func();
        }
        return ReturnResultType::Failure(ExtractError());
    }

    /**
     * @brief 纯值映射算子（左值版本）：成功时执行无参闭包并映射为新结果，失败时短路透传错误
     * @tparam F 返回普通值的无参闭包
     * @param Func 映射闭包
     * @return 包含映射后新值的结果实例
     */
    template <typename F>
    auto Map(F&& Func) const & {
        using NewSuccessType = std::invoke_result_t<F>;
        if constexpr (std::is_void_v<NewSuccessType>) {
            if (IsSuccess()) {
                Func();
                return TGameResult<void, ErrorType>::Success();
            }
            return TGameResult<void, ErrorType>::Failure(GetError());
        } else {
            using ReturnResultType = TGameResult<NewSuccessType, ErrorType>;
            if (IsSuccess()) {
                return ReturnResultType::Success(Func());
            }
            return ReturnResultType::Failure(GetError());
        }
    }

    /**
     * @brief 纯值映射算子（右值移动版本）：成功时执行无参闭包并映射为新结果，失败时移动透传错误
     * @tparam F 返回普通值的无参闭包
     * @param Func 映射闭包
     * @return 包含映射后新值的结果实例
     */
    template <typename F>
    auto Map(F&& Func) && {
        using NewSuccessType = std::invoke_result_t<F>;
        if constexpr (std::is_void_v<NewSuccessType>) {
            if (IsSuccess()) {
                Func();
                return TGameResult<void, ErrorType>::Success();
            }
            return TGameResult<void, ErrorType>::Failure(ExtractError());
        } else {
            using ReturnResultType = TGameResult<NewSuccessType, ErrorType>;
            if (IsSuccess()) {
                return ReturnResultType::Success(Func());
            }
            return ReturnResultType::Failure(ExtractError());
        }
    }

    /**
     * @brief 错误映射算子（左值版本）：失败时将错误载荷转换为新错误类型，成功时原样透传
     * @tparam F 接收 const ErrorType& 并返回新错误对象的闭包
     * @param Func 错误转换闭包
     * @return 携带转换后错误的结果实例
     */
    template <typename F>
    auto MapError(F&& Func) const & {
        using NewErrorType = std::invoke_result_t<F, const ErrorType&>;
        using ReturnResultType = TGameResult<void, NewErrorType>;
        if (IsSuccess()) {
            return ReturnResultType::Success();
        }
        return ReturnResultType::Failure(Func(GetError()));
    }

    /**
     * @brief 错误映射算子（右值移动版本）：失败时移动错误载荷并转换为新错误类型，成功时移动透传
     * @tparam F 接收 ErrorType&& 并返回新错误对象的闭包
     * @param Func 错误转换闭包
     * @return 携带转换后错误的结果实例
     */
    template <typename F>
    auto MapError(F&& Func) && {
        using NewErrorType = std::invoke_result_t<F, ErrorType>;
        using ReturnResultType = TGameResult<void, NewErrorType>;
        if (IsSuccess()) {
            return ReturnResultType::Success();
        }
        return ReturnResultType::Failure(Func(ExtractError()));
    }

    /**
     * @brief 错误恢复算子（左值版本）：失败时调用闭包进行降级或兜底恢复，成功时原样返回
     * @tparam F 接收 const ErrorType& 并返回 TGameResult<void, ErrorType> 的闭包
     * @param RecoveryFunc 错误恢复闭包
     * @return 恢复或原样结果实例
     */
    template <typename F>
    TGameResult OrElse(F&& RecoveryFunc) const & {
        if (IsSuccess()) {
            return *this;
        }
        return RecoveryFunc(GetError());
    }

    /**
     * @brief 错误恢复算子（右值移动版本）：失败时移动错误载荷并调用闭包进行降级或兜底恢复，成功时移动返回
     * @tparam F 接收 ErrorType&& 并返回 TGameResult<void, ErrorType> 的闭包
     * @param RecoveryFunc 错误恢复闭包
     * @return 恢复或原样结果实例
     */
    template <typename F>
    TGameResult OrElse(F&& RecoveryFunc) && {
        if (IsSuccess()) {
            return MoveTemp(*this);
        }
        return RecoveryFunc(ExtractError());
    }

    /**
     * @brief 构造无返回值的成功结果实例（零错误对象构造开销）
     * @return 成功态结果实例
     */
    [[nodiscard]] static TGameResult Success() noexcept {
        return TGameResult();
    }

    /**
     * @brief 构造携带错误信息的失败结果实例
     * @param InError 错误载荷
     * @return 失败态结果实例
     */
    [[nodiscard]] static TGameResult Failure(ErrorType InError) {
        return TGameResult(MoveTemp(InError));
    }

    /**
     * @brief 快速构造结构化 FGameError 失败结果（错误码 + 语义描述）
     * @tparam E 错误类型推导占位
     * @param InErrorCode 模块内错误码
     * @param InMessage 语义错误说明
     * @return 失败态结果实例
     */
    template <typename E = TError, typename = std::enable_if_t<std::is_same_v<E, FGameError>>>
    [[nodiscard]] static TGameResult Failure(int32 InErrorCode, FString InMessage) {
        return Failure(FGameError(Game::ModuleId::Core, InErrorCode, EGameErrorSeverity::Error, MoveTemp(InMessage)));
    }

    /**
     * @brief 快速构造结构化 FGameError 失败结果（模块 ID + 错误码 + 语义描述）
     * @tparam E 错误类型推导占位
     * @param InModuleId 系统模块 ID
     * @param InErrorCode 模块内错误码
     * @param InMessage 语义错误说明
     * @return 失败态结果实例
     */
    template <typename E = TError, typename = std::enable_if_t<std::is_same_v<E, FGameError>>>
    [[nodiscard]] static TGameResult Failure(int32 InModuleId, int32 InErrorCode, FString InMessage) {
        return Failure(FGameError(InModuleId, InErrorCode, EGameErrorSeverity::Error, MoveTemp(InMessage)));
    }

    /**
     * @brief 快速构造结构化 FGameError 失败结果（模块 ID + 错误码 + 严重级别 + 语义描述）
     * @tparam E 错误类型推导占位
     * @param InModuleId 系统模块 ID
     * @param InErrorCode 模块内错误码
     * @param InSeverity 错误严重级别
     * @param InMessage 语义错误说明
     * @return 失败态结果实例
     */
    template <typename E = TError, typename = std::enable_if_t<std::is_same_v<E, FGameError>>>
    [[nodiscard]] static TGameResult Failure(
        int32 InModuleId,
        int32 InErrorCode,
        EGameErrorSeverity InSeverity,
        FString InMessage
    ) {
        return Failure(FGameError(InModuleId, InErrorCode, InSeverity, MoveTemp(InMessage)));
    }

private:
    TGameResult() noexcept = default;

    explicit TGameResult(ErrorType InError)
        : Error(MoveTemp(InError)) {
    }

private:
    TOptional<ErrorType> Error;
};

/**
 * @brief 基于标准化 FGameError 契约的状态结果类型别名
 * @tparam TSuccess 成功载荷类型
 */
template <typename TSuccess>
using TGameStatusResult = TGameResult<TSuccess, FGameError>;

namespace Game::Private {

/**
 * @brief 根据结果表达式的引用与常量类型自动以移动或拷贝方式转发错误载荷
 * @tparam TResult 结果类型包
 * @param Result 结果实例引用
 * @return 错误转发器包裹体
 */
template <typename TResult>
[[nodiscard]] auto ForwardGameResultError(TResult&& Result) {
    using DecayedType = std::remove_cvref_t<TResult>;
    using ErrorType = typename DecayedType::ErrorType;
    if constexpr (std::is_rvalue_reference_v<TResult&&> && !std::is_const_v<std::remove_reference_t<TResult>>) {
        return TGameErrorForwarder<ErrorType>(Result.ExtractError());
    } else {
        return TGameErrorForwarder<ErrorType>(Result.GetError());
    }
}

/**
 * @brief 根据结果表达式类型自动追加调用栈上下文并转发错误载荷
 * @tparam TResult 结果类型包
 * @tparam TContext 上下文文本类型
 * @param Result 结果实例引用
 * @param Context 追加的上下文描述
 * @return 错误转发器包裹体
 */
template <typename TResult, typename TContext>
[[nodiscard]] auto ForwardGameResultErrorWithContext(TResult&& Result, TContext&& Context) {
    using DecayedType = std::remove_cvref_t<TResult>;
    using ErrorType = typename DecayedType::ErrorType;
    if constexpr (std::is_rvalue_reference_v<TResult&&> && !std::is_const_v<std::remove_reference_t<TResult>>) {
        return TGameErrorForwarder<ErrorType>(Result.ExtractError().WithContext(Forward<TContext>(Context)));
    } else {
        return TGameErrorForwarder<ErrorType>(Result.GetError().WithContext(Forward<TContext>(Context)));
    }
}

}  // namespace Game::Private

/**
 * @brief 链式错误向上传播宏（支持常量/左值/右值结果表达式以及跨不同 TSuccess 返回类型的函数间自动错误传播）
 */
#define GAME_TRY(ResultExpr) \
    do { \
        auto&& _GameTryResult = (ResultExpr); \
        if (_GameTryResult.IsFailure()) { \
            return ::Game::Private::ForwardGameResultError(Forward<decltype(_GameTryResult)>(_GameTryResult)); \
        } \
    } while (false)

/**
 * @brief 带调用栈上下文追加的链式错误向上传播宏（针对 FGameError 自动调用 WithContext 后向上返回，支持常量与左值引用）
 */
#define GAME_TRY_CTX(ResultExpr, ContextText) \
    do { \
        auto&& _GameTryResult = (ResultExpr); \
        if (_GameTryResult.IsFailure()) { \
            return ::Game::Private::ForwardGameResultErrorWithContext( \
                Forward<decltype(_GameTryResult)>(_GameTryResult), (ContextText)); \
        } \
    } while (false)


