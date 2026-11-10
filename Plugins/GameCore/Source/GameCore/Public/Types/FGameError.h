/**
 * @file FGameError.h
 * @brief 强类型跨模块错误契约结构体（支持模块 ID、错误码、语义文本与严重性分级）
 */

#pragma once

#include "CoreMinimal.h"

#include "FGameError.generated.h"

/**
 * @brief 系统模块 ID 常量定义表
 */
namespace Game::ModuleId {
    inline constexpr int32 Unknown   = 0;
    inline constexpr int32 Core      = 1;
    inline constexpr int32 Net       = 2;
    inline constexpr int32 Asset     = 3;
    inline constexpr int32 Feedback  = 4;
    inline constexpr int32 Pool      = 5;
    inline constexpr int32 Spatial   = 6;
    inline constexpr int32 Message   = 7;
    inline constexpr int32 Flow      = 8;
    inline constexpr int32 Logging   = 9;
    inline constexpr int32 Data      = 10;
    inline constexpr int32 Gameplay  = 11;
}  // namespace Game::ModuleId

/**
 * @brief 通用错误码定义
 */
namespace Game::CommonErrorCodes {
    inline constexpr int32 Success          = 0;
    inline constexpr int32 InvalidParameter = 1;
    inline constexpr int32 OperationFailed  = 2;
    inline constexpr int32 Timeout          = 3;
    inline constexpr int32 NotFound         = 4;
    inline constexpr int32 PermissionDenied = 5;
    inline constexpr int32 Cancelled        = 6;
    inline constexpr int32 InvalidState     = 7;
}  // namespace Game::CommonErrorCodes

/**
 * @brief 错误严重级别枚举
 */
UENUM(BlueprintType)
enum class EGameErrorSeverity : uint8 {
    Success  UMETA(DisplayName = "Success"),
    Info     UMETA(DisplayName = "Info"),
    Warning  UMETA(DisplayName = "Warning"),
    Error    UMETA(DisplayName = "Error"),
    Critical UMETA(DisplayName = "Critical")
};

/**
 * @brief 全局解耦错误契约结构体
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FGameError {
    GENERATED_BODY()

    /**
     * @brief 产生错误的系统模块 ID
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Error")
    int32 ModuleId = Game::ModuleId::Unknown;

    /**
     * @brief 模块内部自定义错误码（0 表示成功，非 0 表示特定错误）
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Error")
    int32 ErrorCode = 0;

    /**
     * @brief 错误严重级别
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Error")
    EGameErrorSeverity Severity = EGameErrorSeverity::Success;

    /**
     * @brief 人类可读错误详细说明
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Error")
    FString Message;

    FGameError() = default;

    /**
     * @brief 完整参数构造函数
     * @param InModuleId 系统模块 ID
     * @param InErrorCode 模块错误码
     * @param InSeverity 严重级别
     * @param InMessage 详细描述文案
     */
    FGameError(int32 InModuleId, int32 InErrorCode, EGameErrorSeverity InSeverity, FString InMessage)
        : ModuleId(InModuleId)
        , ErrorCode(InErrorCode)
        , Severity(InSeverity)
        , Message(MoveTemp(InMessage)) {
    }

    /**
     * @brief 快速构造成功实例
     * @return 成功态 FGameError
     */
    [[nodiscard]] static FGameError Ok() noexcept {
        return FGameError(Game::ModuleId::Core, 0, EGameErrorSeverity::Success, FString());
    }

    /**
     * @brief 快速构造普通错误实例
     * @param InModuleId 产生错误的模块 ID
     * @param InErrorCode 错误码
     * @param InMessage 详细描述文案
     * @param InSeverity 严重级别（默认 Error）
     * @return 错误实例
     */
    [[nodiscard]] static FGameError Make(
        int32 InModuleId,
        int32 InErrorCode,
        FString InMessage,
        EGameErrorSeverity InSeverity = EGameErrorSeverity::Error
    ) {
        return FGameError(InModuleId, InErrorCode, InSeverity, MoveTemp(InMessage));
    }

    /**
     * @brief 检查当前是否为成功态
     * @return 成功返回 true
     */
    [[nodiscard]] bool IsOk() const noexcept {
        return Severity == EGameErrorSeverity::Success && ErrorCode == 0;
    }

    /**
     * @brief 检查当前是否为错误态
     * @return 存在错误返回 true
     */
    [[nodiscard]] bool IsError() const noexcept {
        return !IsOk();
    }

    /**
     * @brief 检查当前是否为致命级错误
     * @return 致命级返回 true
     */
    [[nodiscard]] bool IsCritical() const noexcept {
        return Severity == EGameErrorSeverity::Critical;
    }

    /**
     * @brief 追加调用链上下文说明并返回更新后的错误对象（支持链式错误传播追加上下文）
     * @param InContext 当前层级上下文说明
     * @return 附加了上下文说明的错误实例
     */
    [[nodiscard]] FGameError WithContext(const FString& InContext) const {
        FGameError Updated = *this;
        if (!InContext.IsEmpty()) {
            Updated.Message = Updated.Message.IsEmpty()
                ? InContext
                : FString::Printf(TEXT("%s [%s]"), *Updated.Message, *InContext);
        }
        return Updated;
    }

    /**
     * @brief 格式化为可打印诊断字符串
     * @return 格式化文本
     */
    [[nodiscard]] FString ToString() const {
        return FString::Printf(
            TEXT("[Module:%d Code:%d Severity:%d] %s"),
            ModuleId,
            ErrorCode,
            static_cast<int32>(Severity),
            *Message
        );
    }

    bool operator==(const FGameError& Other) const = default;
};
