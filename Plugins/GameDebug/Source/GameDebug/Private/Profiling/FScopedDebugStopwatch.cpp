/**
 * @file FScopedDebugStopwatch.cpp
 * @brief 现代化 RAII 作用域耗时诊断秒表实现
 */

#include "Profiling/FScopedDebugStopwatch.h"

#include "Config/GameCompileSwitches.h"
#include "Logging/GameLogCategories.h"
#include "Misc/StringBuilder.h"
#include "Print/FGameDebugPrint.h"

#if GAME_ENABLE_CPU_PROFILER != 0

FScopedDebugStopwatch::FScopedDebugStopwatch(
    FName InScopeName,
    double InThresholdMs,
    FName InChannel
)
    : ScopeName(InScopeName)
    , ThresholdMs((FMath::IsFinite(InThresholdMs) && InThresholdMs > 0.0) ? InThresholdMs : 0.0)
    , Channel(InChannel) {
}

FScopedDebugStopwatch::~FScopedDebugStopwatch() {
    const double ElapsedMs = GetElapsedMs();
    if (ElapsedMs < ThresholdMs) {
        return;
    }

    TStringBuilder<256> Builder;
    Builder.Append(TEXT("【GameDebug】【Profiler】["));
    ScopeName.AppendString(Builder);
    Builder.Appendf(TEXT("] 耗时 %.3f ms (告警阈值: %.2f ms)"), ElapsedMs, ThresholdMs);

    const FString FormattedText(Builder.ToView());
    if constexpr (Game::Config::bEnableDebugDraw) {
        const bool bIsSevere = (ThresholdMs > 0.0) && (ElapsedMs >= ThresholdMs * SevereThresholdMultiplier);
        const FColor MsgColor = bIsSevere ? FColor::Red : FColor::Orange;
        FGameDebugPrint::PrintChannel(Channel, FormattedText, MsgColor, AlertDisplayDurationSeconds);
    }

    GAME_LOG(Warning, TEXT("%s"), *FormattedText);
}

double FScopedDebugStopwatch::GetElapsedMs() const noexcept {
    return CpuTimer.GetElapsedMilliseconds();
}

#else

FScopedDebugStopwatch::FScopedDebugStopwatch(
    [[maybe_unused]] FName InScopeName,
    [[maybe_unused]] double InThresholdMs,
    [[maybe_unused]] FName InChannel
) {
}

FScopedDebugStopwatch::~FScopedDebugStopwatch() = default;

double FScopedDebugStopwatch::GetElapsedMs() const noexcept {
    return 0.0;
}

#endif
