/**
 * @file FGameTypeFingerprint.h
 * @brief 跨动态库稳定的编译期类型指纹（非 RTTI，供监听总线与观察者注册表共享）
 */

#pragma once

#include "CoreMinimal.h"
#include "Hash/CityHash.h"

#include <type_traits>

namespace Game::Core {

/**
 * @brief 可选显式类型名称特征模板（允许对特定类型特化固定名称以跨越不同编译器签名格式）
 * @tparam T 规范化后的目标类型
 */
template <typename T>
struct TGameCustomTypeFingerprint {
    static constexpr const char* TypeName = nullptr;
};

/**
 * @brief 内部规范化类型签名哈希计算器（T 已剥离 cv 与引用限定符，自动消解 typedef/using 别名差异）
 * @tparam TCanonical 规范化后的底层类型
 * @return 64 位稳定 CityHash 指纹
 */
template <typename TCanonical>
[[nodiscard]] inline uint64 GetCanonicalTypeFingerprint() noexcept {
    if constexpr (TGameCustomTypeFingerprint<TCanonical>::TypeName != nullptr) {
        const char* CustomName = TGameCustomTypeFingerprint<TCanonical>::TypeName;
        static const uint64 CustomId = CityHash64(CustomName, static_cast<uint32>(FCStringAnsi::Strlen(CustomName)));
        return CustomId;
    } else {
#if defined(_MSC_VER)
        const char* TypeSignature = __FUNCSIG__;
#elif defined(__clang__) || defined(__GNUC__)
        const char* TypeSignature = __PRETTY_FUNCTION__;
#else
        const char* TypeSignature = __func__;
#endif
        static const uint64 TypeId = CityHash64(TypeSignature, static_cast<uint32>(FCStringAnsi::Strlen(TypeSignature)));
        return TypeId;
    }
}

/**
 * @brief 获取类型指纹（先归一化剥离 cv/引用与类型别名，再计算 64 位稳定哈希，跨 DLL/dylib 一致）
 * @tparam T 目标类型
 * @return 类型指纹
 */
template<typename T>
[[nodiscard]] inline uint64 GetTypeFingerprint() noexcept {
    using FCanonicalType = std::remove_cv_t<std::remove_reference_t<T>>;
    return GetCanonicalTypeFingerprint<FCanonicalType>();
}

} // namespace Game::Core
