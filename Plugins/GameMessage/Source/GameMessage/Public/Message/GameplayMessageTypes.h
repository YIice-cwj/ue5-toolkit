// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <atomic>

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
    /**
     * @brief 精确匹配：仅当广播通道与监听通道完全相同时触发
     */
    ExactMatch,

    /**
     * @brief 部分/前缀匹配：广播通道为监听通道的子标签（MatchesTag）时也会触发
     */
    PartialMatch
};

/**
 * @brief 消息监听句柄：可安全拷贝，全生命周期离开后通过共享引用 RAII 自动注销
 */
USTRUCT(BlueprintType)
struct GAMEMESSAGE_API FGameplayMessageListenerHandle {
    GENERATED_BODY()

private:
    friend class UGameplayMessageSubsystem;

    struct FListenerRegistration {
        FListenerRegistration() = default;
        FListenerRegistration(const FListenerRegistration&) = delete;
        FListenerRegistration& operator=(const FListenerRegistration&) = delete;
        FListenerRegistration(FListenerRegistration&&) = delete;
        FListenerRegistration& operator=(FListenerRegistration&&) = delete;
        ~FListenerRegistration();

        void Unregister();

        TWeakObjectPtr<UGameplayMessageSubsystem> SubsystemWeak;
        FGameplayTag Channel;
        std::atomic<int64> ListenerId{0};
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

    /**
     * @brief 比较两个句柄是否指向同一底层监听注册
     * @param Other 待比较句柄
     * @return 指向同一注册返回 true
     */
    [[nodiscard]] bool operator==(const FGameplayMessageListenerHandle& Other) const noexcept {
        return Registration == Other.Registration;
    }

    /**
     * @brief 比较两个句柄是否指向不同底层监听注册
     * @param Other 待比较句柄
     * @return 指向不同注册返回 true
     */
    [[nodiscard]] bool operator!=(const FGameplayMessageListenerHandle& Other) const noexcept {
        return Registration != Other.Registration;
    }

    /**
     * @brief 计算句柄哈希值以支持 TSet 与 TMap 容器
     * @param Handle 待哈希句柄
     * @return 内部注册指针哈希值
     */
    [[nodiscard]] friend uint32 GetTypeHash(const FGameplayMessageListenerHandle& Handle) noexcept {
        return GetTypeHash(Handle.Registration);
    }

private:
    FGameplayMessageListenerHandle(UGameplayMessageSubsystem* InSubsystem, FGameplayTag InChannel, int64 InId);

private:
    TSharedPtr<FListenerRegistration> Registration;
};
