/**
 * @file FScopedDebugStopwatch.cpp
 * @brief 现代化 RAII 作用域耗时诊断秒表实现
 */

#include "Profiling/FScopedDebugStopwatch.h"

#include "Config/GameCompileSwitches.h"
#include "HAL/PlatformTime.h"
#include "Print/FGameDebugPrint.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameProfiler, Log, All);

FScopedDebugStopwatch::FScopedDebugStopwatch(
    const FName& InScopeName,
    float InThresholdMs,
    const FName& InChannel
)
    : ScopeName(InScopeName)
    , ThresholdMs(InThresholdMs)
    , Channel(InChannel)
    , StartTimeSeconds(0.0) {
    if constexpr (Game::Config::bEnableDebugDraw) {
        StartTimeSeconds = FPlatformTime::Seconds();
    }
}

FScopedDebugStopwatch::~FScopedDebugStopwatch() {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return;
    } else {
        const float ElapsedMs = GetElapsedMs();
        if (ElapsedMs >= ThresholdMs) {
            const FString FormattedText = FString::Printf(
                TEXT("【性能诊断】[%s] 耗时 %.3f ms (告警阈值: %.2f ms)"),
                *ScopeName.ToString(),
                ElapsedMs,
                ThresholdMs
            );

            const FColor MsgColor = (ElapsedMs >= ThresholdMs * 2.0f && ThresholdMs > 0.0f)
                ? FColor::Red
                : FColor::Orange;

            FGameDebugPrint::PrintChannel(Channel, FormattedText, MsgColor, 3.0f);

            if constexpr (Game::Config::bEnableLog) {
                UE_LOG(LogGameProfiler, Warning, TEXT("%s"), *FormattedText);
            }
        }
    }
}

float FScopedDebugStopwatch::GetElapsedMs() const {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return 0.0f;
    } else {
        const double Now = FPlatformTime::Seconds();
        return static_cast<float>((Now - StartTimeSeconds) * 1000.0);
    }
}
