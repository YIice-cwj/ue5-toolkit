/**
 * @file GameDebugScreenKeys.h
 * @brief 屏幕调试消息 64 位域盐槽位键计算工具（确保在 UEngine::ScreenMessages 的 int32 键截断下亦严格域隔离且永不碰撞 InvalidScreenKey）
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FGameTypeFingerprint.h"

namespace Game::Debug::Private {

inline constexpr uint64 InvalidScreenKey = MAX_uint64;
inline constexpr uint64 PrintChannelKeyNamespace = 0x4744500000000000ULL;
inline constexpr uint64 WatchHubKeyNamespace = 0x4744570000000000ULL;
inline constexpr uint32 Low31BitMask = 0x7FFFFFFFU;
inline constexpr uint32 HighSignBitMask = 0x80000000U;
inline constexpr uint32 InvalidLow32Sentinel = 0xFFFFFFFFU;
inline constexpr uint32 WatchHubFallbackLow32 = 0xFFFFFFFEU;

/**
 * @brief 计算 FGameDebugPrint 通道专属的 64 位屏幕槽位键（低 32 位恒落入 [0, 0x7FFFFFFF]，与 WatchHub 数学正交）
 * @param Channel 通道名称
 * @return 64 位槽位键；NAME_None 返回 InvalidScreenKey
 */
[[nodiscard]] inline uint64 MakePrintChannelKey(FName Channel) noexcept {
    if (Channel.IsNone()) {
        return InvalidScreenKey;
    }
    const uint32 RawHash = GetTypeHash(Channel);
    const uint32 Mixed = Game::Hash::Hash64To32(PrintChannelKeyNamespace ^ static_cast<uint64>(RawHash));
    const uint32 DomainLow32 = Mixed & Low31BitMask;
    return PrintChannelKeyNamespace | static_cast<uint64>(DomainLow32);
}

/**
 * @brief 计算 FGameDebugWatchHub 分类专属的 64 位屏幕槽位键（低 32 位恒落入 [0x80000000, 0xFFFFFFFE]，永不与 Print 或 -1 冲突）
 * @param Category 观察分类名称
 * @return 64 位槽位键；NAME_None 返回 InvalidScreenKey
 */
[[nodiscard]] inline uint64 MakeWatchHubCategoryKey(FName Category) noexcept {
    if (Category.IsNone()) {
        return InvalidScreenKey;
    }
    const uint32 RawHash = GetTypeHash(Category);
    const uint32 Mixed = Game::Hash::Hash64To32(WatchHubKeyNamespace ^ static_cast<uint64>(RawHash));
    uint32 DomainLow32 = HighSignBitMask | (Mixed & Low31BitMask);
    if (DomainLow32 == InvalidLow32Sentinel) {
        DomainLow32 = WatchHubFallbackLow32;
    }
    return WatchHubKeyNamespace | static_cast<uint64>(DomainLow32);
}

}  // namespace Game::Debug::Private
