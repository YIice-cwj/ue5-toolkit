/**
 * @file TGameResult.h
 * @brief 无异常强类型结果容器模板（成功值或错误载荷二选一，零堆分配）
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/TVariant.h"

/**
 * @brief 无异常强类型结果模型（支持相同成功/错误类型实例化）
 * @tparam TSuccess 成功载荷类型
 * @tparam TError 错误载荷类型（默认 FText）
 */
template <typename TSuccess, typename TError = FText>
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
     * @brief 获取成功载荷可变引用（调用前须处于成功态）
     * @return 成功载荷可变引用
     */
    [[nodiscard]] SuccessType& GetMutableValue();

    /**
     * @brief 获取成功载荷副本，若处于失败态则返回回退值
     * @param FallbackValue 失败时的回退值
     * @return 成功载荷或回退值
     */
    [[nodiscard]] SuccessType GetValueOr(SuccessType FallbackValue) const;

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

private:
    TGameResult(ESuccessTag, SuccessType InValue);
    TGameResult(EErrorTag, ErrorType InError);

public:
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
typename TGameResult<TSuccess, TError>::SuccessType TGameResult<TSuccess, TError>::GetValueOr(SuccessType FallbackValue) const {
    if (IsSuccess()) {
        return Storage.template Get<FSuccessStorage>().Value;
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
