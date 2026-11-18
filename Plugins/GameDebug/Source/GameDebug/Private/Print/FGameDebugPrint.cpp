/**
 * @file FGameDebugPrint.cpp
 * @brief 屏幕与通道调试打印工具实现
 */

#include "Print/FGameDebugPrint.h"

#include "Config/GameCompileSwitches.h"
#include "Engine/Engine.h"

void FGameDebugPrint::Print(
    const FString& Message,
    const FColor& Color,
    float Duration,
    uint64 Key
) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    } else {
        if (GEngine != nullptr) {
            GEngine->AddOnScreenDebugMessage(Key, Duration, Color, Message);
        }
    }
}

void FGameDebugPrint::PrintChannel(
    const FName& Channel,
    const FString& Message,
    const FColor& Color,
    float Duration
) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    } else {
        const uint64 Key = GetChannelKey(Channel);
        const FString FormattedMessage = FString::Printf(TEXT("[%s] %s"), *Channel.ToString(), *Message);
        Print(FormattedMessage, Color, Duration, Key);
    }
}

void FGameDebugPrint::PrintWarning(const FString& Message, float Duration) {
    Print(Message, FColor::Orange, Duration);
}

void FGameDebugPrint::PrintError(const FString& Message, float Duration) {
    Print(Message, FColor::Red, Duration);
}

uint64 FGameDebugPrint::GetChannelKey(const FName& Channel) {
    if (Channel.IsNone()) {
        return static_cast<uint64>(-1);
    }
    return static_cast<uint64>(GetTypeHash(Channel));
}
