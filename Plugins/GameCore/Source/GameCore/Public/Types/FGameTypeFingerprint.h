/**
 * @file FGameTypeFingerprint.h
 * @brief 跨动态库稳定的编译期类型指纹与底层零分配散列/整数解析工具
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/StringView.h"
#include "Hash/CityHash.h"

#include <type_traits>

namespace Game::Hash {

/**
 * @brief 64 位整数极致雪崩散列算法（MurmurHash3 64-bit Finalizer，单周期无分支位运算）
 * @param Value 待散列 64 位数值
 * @return 散列均匀的 32 位哈希码
 */
[[nodiscard]] inline constexpr uint32 Hash64To32(uint64 Value) noexcept {
    Value ^= Value >> 33;
    Value *= 0xff51afd7ed558ccdULL;
    Value ^= Value >> 33;
    Value *= 0xc4ceb9fe1a85ec53ULL;
    Value ^= Value >> 33;
    return static_cast<uint32>(Value ^ (Value >> 32));
}

} // namespace Game::Hash

namespace Game::Private {

/**
 * @brief 零堆分配高性能解析十进制或 0x 前缀十六进制无符号 64 位整数
 * @param InView 输入字符串切片
 * @param OutValue 传出解析结果
 * @return 解析成功且无溢出返回 true，存在非法字符或溢出返回 false
 */
inline bool ParseUInt64FromView(FStringView InView, uint64& OutValue) noexcept {
    InView = InView.TrimStartAndEnd();
    if (InView.IsEmpty()) {
        return false;
    }
    bool bHex = false;
    if (InView.StartsWith(TEXT("0x"), ESearchCase::IgnoreCase)) {
        bHex = true;
        InView.RemovePrefix(2);
        if (InView.IsEmpty()) {
            return false;
        }
    }
    uint64 Result = 0;
    if (bHex) {
        for (const TCHAR Ch : InView) {
            uint64 Digit = 0;
            if (Ch >= TEXT('0') && Ch <= TEXT('9')) {
                Digit = Ch - TEXT('0');
            } else if (Ch >= TEXT('a') && Ch <= TEXT('f')) {
                Digit = 10 + (Ch - TEXT('a'));
            } else if (Ch >= TEXT('A') && Ch <= TEXT('F')) {
                Digit = 10 + (Ch - TEXT('A'));
            } else {
                return false;
            }
            if (Result > (0xFFFFFFFFFFFFFFFFULL >> 4)) {
                return false;
            }
            Result = (Result << 4) | Digit;
        }
    } else {
        for (const TCHAR Ch : InView) {
            if (Ch < TEXT('0') || Ch > TEXT('9')) {
                return false;
            }
            const uint64 Digit = Ch - TEXT('0');
            if (Result > (0xFFFFFFFFFFFFFFFFULL - Digit) / 10ULL) {
                return false;
            }
            Result = Result * 10ULL + Digit;
        }
    }
    OutValue = Result;
    return true;
}

} // namespace Game::Private

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
template <typename T>
[[nodiscard]] inline uint64 GetTypeFingerprint() noexcept {
    using FCanonicalType = std::remove_cv_t<std::remove_reference_t<T>>;
    return GetCanonicalTypeFingerprint<FCanonicalType>();
}

} // namespace Game::Core
