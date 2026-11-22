/**
 * @file FGameplayMessageListenerHandle.h
 * @brief 消息总线监听订阅 RAII 句柄声明（支持安全拷贝、跨线程退订与子系统 Epoch 失效感知）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Templates/SharedPointer.h"

#include "FGameplayMessageListenerHandle.generated.h"

class UGameplayMessageSubsystem;

/**
 * @brief 消息监听句柄：可安全拷贝，全生命周期离开后通过共享引用 RAII 自动注销
 */
USTRUCT(BlueprintType)
struct GAMEMESSAGE_API FGameplayMessageListenerHandle {
    GENERATED_BODY()

public:
    FGameplayMessageListenerHandle() = default;

    /**
     * @brief 手动注销监听并清空句柄
     */
    void Unregister();

    /**
     * @brief 检查句柄当前是否指向活跃监听（线程安全，子系统 Deinitialize 后立即失效）
     * @return 有效返回 true
     */
    [[nodiscard]] bool IsValid() const noexcept;

    /**
     * @brief 布尔显式类型转换操作符（等价于 IsValid()）
     * @return 有效返回 true
     */
    [[nodiscard]] explicit operator bool() const noexcept {
        return IsValid();
    }

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
    friend class UGameplayMessageSubsystem;

    struct FListenerRegistration;

    /**
     * @brief 内部私有构造函数（仅由 UGameplayMessageSubsystem 签发）
     * @param InSubsystem 消息子系统实例
     * @param InChannel 注册通道
     * @param InId 注册监听器 ID
     * @param InActiveEpoch 所属子系统当前运行纪元弱引用
     */
    FGameplayMessageListenerHandle(
        UGameplayMessageSubsystem* InSubsystem,
        FGameplayTag InChannel,
        int64 InId,
        TWeakPtr<const void> InActiveEpoch
    );

private:
    TSharedPtr<FListenerRegistration> Registration;
};
