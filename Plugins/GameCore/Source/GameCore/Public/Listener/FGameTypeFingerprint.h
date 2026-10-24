/**
 * @file FGameTypeFingerprint.h
 * @brief 跨动态库稳定的编译期类型指纹（非 RTTI，供监听总线与观察者注册表共享）
 */

#pragma once

#include "CoreMinimal.h"
#include "Hash/CityHash.h"

#if defined(_MSC_VER)
    #define GAME_TYPE_FINGERPRINT_SIGNATURE __FUNCSIG__
#elif defined(__clang__) || defined(__GNUC__)
    #define GAME_TYPE_FINGERPRINT_SIGNATURE __PRETTY_FUNCTION__
#else
    #define GAME_TYPE_FINGERPRINT_SIGNATURE __func__
#endif

namespace Game::Core {

/**
 * @brief 获取类型指纹（编译期类型签名的 64 位稳定哈希，跨 DLL/dylib 一致）
 * @tparam T 目标类型
 * @return 类型指纹
 */
template<typename T>
[[nodiscard]] inline uint64 GetTypeFingerprint() noexcept {
    const char* TypeSignature = GAME_TYPE_FINGERPRINT_SIGNATURE;
    static const uint64 TypeId = CityHash64(TypeSignature, static_cast<uint32>(FCStringAnsi::Strlen(TypeSignature)));
    return TypeId;
}

} // namespace Game::Core

#undef GAME_TYPE_FINGERPRINT_SIGNATURE
