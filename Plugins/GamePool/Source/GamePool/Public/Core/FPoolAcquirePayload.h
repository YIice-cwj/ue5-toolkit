/**
 * @file FPoolAcquirePayload.h
 * @brief 对象池借出零堆分配强类型载荷视图（基于 GameCore 编译期类型指纹防误解包）
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FGameTypeFingerprint.h"

/**
 * @brief 16 字节栈上不可变业务载荷视图（携带编译期 64 位类型指纹，彻底杜绝裸 void* 类型错配 UB）
 */
struct GAMEPOOL_API FPoolAcquirePayload {
public:
    /**
     * @brief 默认构造空载荷视图
     */
    constexpr FPoolAcquirePayload() noexcept = default;

    /**
     * @brief 从强类型业务载荷引用构造只读载荷视图（零堆分配，不转移所有权）
     * @tparam TPayload 业务载荷具体类型
     * @param InPayload 业务载荷常量引用
     * @return 携带该类型指纹的载荷视图
     */
    template <typename TPayload>
    [[nodiscard]] static FPoolAcquirePayload From(const TPayload& InPayload) noexcept {
        return FPoolAcquirePayload(&InPayload, Game::Core::GetTypeFingerprint<TPayload>());
    }

    /**
     * @brief 查询当前视图是否携带有效载荷指针与类型指纹
     * @return 载荷非空且指纹非零返回 true
     */
    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return Data != nullptr && TypeFingerprint != 0ULL;
    }

    /**
     * @brief 查询当前载荷是否精确匹配指定类型
     * @tparam TPayload 待检验目标载荷类型
     * @return 载荷有效且类型指纹一致返回 true
     */
    template <typename TPayload>
    [[nodiscard]] bool IsType() const noexcept {
        return IsValid() && TypeFingerprint == Game::Core::GetTypeFingerprint<TPayload>();
    }

    /**
     * @brief 类型安全解包载荷指针（自动校验 64 位类型指纹，不匹配时安全返回 nullptr）
     * @tparam TPayload 目标载荷类型
     * @return 匹配时返回强类型不可变指针，空载荷或类型不匹配时返回 nullptr
     */
    template <typename TPayload>
    [[nodiscard]] const TPayload* Extract() const noexcept {
        if (!IsType<TPayload>()) {
            return nullptr;
        }
        return static_cast<const TPayload*>(Data);
    }

    /**
     * @brief 获取载荷的 64 位类型指纹
     * @return 类型指纹值，空载荷返回 0
     */
    [[nodiscard]] constexpr uint64 GetTypeFingerprint() const noexcept {
        return TypeFingerprint;
    }

private:
    constexpr FPoolAcquirePayload(const void* InData, uint64 InTypeFingerprint) noexcept
        : Data(InData)
        , TypeFingerprint(InTypeFingerprint) {
    }

private:
    const void* Data = nullptr;
    uint64 TypeFingerprint = 0ULL;
};
