/**
 * @file FGameDebugPrint.cpp
 * @brief 屏幕与通道调试打印工具实现
 */

#include "Print/FGameDebugPrint.h"

#include "Config/GameCompileSwitches.h"
#include "Core/GameDebugScreenKeys.h"
#include "Engine/Engine.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "Misc/StringBuilder.h"

#if WITH_AUTOMATION_TESTS
namespace Game::Debug::Private {

using FPrintObserverFn = void (*)(uint64 Key, const FString& Message, const FColor& Color, float Duration, void* UserData);

namespace {

FCriticalSection GPrintTestObserverLock;
FPrintObserverFn GPrintTestObserver = nullptr;
void* GPrintTestObserverUserData = nullptr;

}  // namespace

void SetPrintTestObserver(FPrintObserverFn Observer, void* UserData) noexcept {
    FScopeLock ScopeLock(&GPrintTestObserverLock);
    GPrintTestObserver = Observer;
    GPrintTestObserverUserData = UserData;
}

}  // namespace Game::Debug::Private
#endif

void FGameDebugPrint::Print(
    const FString& Message,
    const FColor& Color,
    float Duration,
    uint64 Key
) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    }

    if (GEngine == nullptr) {
        return;
    }

    const float SafeDuration = (FMath::IsFinite(Duration) && Duration > 0.0f) ? Duration : 0.0f;
    GEngine->AddOnScreenDebugMessage(Key, SafeDuration, Color, Message);

#if WITH_AUTOMATION_TESTS
    {
        FScopeLock ScopeLock(&Game::Debug::Private::GPrintTestObserverLock);
        if (Game::Debug::Private::GPrintTestObserver != nullptr) {
            Game::Debug::Private::GPrintTestObserver(
                Key,
                Message,
                Color,
                SafeDuration,
                Game::Debug::Private::GPrintTestObserverUserData
            );
        }
    }
#endif
}

void FGameDebugPrint::PrintChannel(
    FName Channel,
    const FString& Message,
    const FColor& Color,
    float Duration
) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    }

    if (GEngine == nullptr) {
        return;
    }

    const uint64 Key = GetChannelKey(Channel);
    TStringBuilder<256> Builder;
    Builder.AppendChar(TEXT('['));
    Channel.AppendString(Builder);
    Builder.Append(TEXT("] "));
    Builder.Append(Message);
    Print(FString(Builder.ToView()), Color, Duration, Key);
}

void FGameDebugPrint::PrintWarning(const FString& Message, float Duration) {
    Print(Message, FColor::Orange, Duration);
}

void FGameDebugPrint::PrintError(const FString& Message, float Duration) {
    Print(Message, FColor::Red, Duration);
}

uint64 FGameDebugPrint::GetChannelKey(FName Channel) noexcept {
    return Game::Debug::Private::MakePrintChannelKey(Channel);
}
