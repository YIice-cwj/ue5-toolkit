// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Templates/SharedPointer.h"

#include "GameplayMessageTypes.generated.h"

class UGameplayMessageSubsystem;

/**
 * @brief 消息通道匹配规则
 */
UENUM(BlueprintType)
enum class EGameplayMessageMatch : uint8 {
    /** 精确匹配：仅当广播通道与监听通道完全相同时触发 */
    ExactMatch,

    /** 部分/前缀匹配：广播通道为监听通道的子标签（MatchesTag）时也会触发 */
    PartialMatch
};

/**
 * @brief 消息监听句柄：可安全拷贝，全生命周期离开后通过共享引用 RAII 自动注销
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FGameplayMessageListenerHandle {
    GENERATED_BODY()

private:
    friend class UGameplayMessageSubsystem;

    struct FListenerRegistration {
        FListenerRegistration() = default;
        FListenerRegistration(const FListenerRegistration&) = default;
        FListenerRegistration& operator=(const FListenerRegistration&) = default;
        FListenerRegistration(FListenerRegistration&&) = default;
        FListenerRegistration& operator=(FListenerRegistration&&) = default;
        ~FListenerRegistration();

        void Unregister();

        TWeakObjectPtr<UGameplayMessageSubsystem> SubsystemWeak;
        FGameplayTag Channel;
        int64 ListenerId = 0;
    };

public:
    FGameplayMessageListenerHandle() = default;

    /**
     * @brief 手动注销监听并清空句柄
     */
    void Unregister();

    /**
     * @brief 检查句柄当前是否有效
     * @return 有效返回 true
     */
    [[nodiscard]] bool IsValid() const noexcept;

private:
    FGameplayMessageListenerHandle(UGameplayMessageSubsystem* InSubsystem, FGameplayTag InChannel, int64 InId);

private:
    TSharedPtr<FListenerRegistration> Registration;
};

