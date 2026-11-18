/**
 * @file FLoadingBarrierToken.cpp
 * @brief 关卡加载幕布异步就绪屏障 RAII 令牌实现
 */

#include "System/FLoadingBarrierToken.h"

#include "System/ULoadingSubsystem.h"

FLoadingBarrierToken::FLoadingBarrierToken(FLoadingBarrierToken&& Other) noexcept
    : SubsystemWeak(MoveTemp(Other.SubsystemWeak))
    , BarrierId(Other.BarrierId)
    , BarrierName(Other.BarrierName)
    , CurrentProgress(Other.CurrentProgress)
    , bReleased(Other.bReleased) {
    Other.bReleased = true;
    Other.BarrierId = 0ULL;
    Other.BarrierName = NAME_None;
    Other.CurrentProgress = 0.0f;
}

FLoadingBarrierToken& FLoadingBarrierToken::operator=(FLoadingBarrierToken&& Other) noexcept {
    if (this != &Other) {
        Release();
        SubsystemWeak = MoveTemp(Other.SubsystemWeak);
        BarrierId = Other.BarrierId;
        BarrierName = Other.BarrierName;
        CurrentProgress = Other.CurrentProgress;
        bReleased = Other.bReleased;
        Other.bReleased = true;
        Other.BarrierId = 0ULL;
        Other.BarrierName = NAME_None;
        Other.CurrentProgress = 0.0f;
    }
    return *this;
}

FLoadingBarrierToken::~FLoadingBarrierToken() {
    Release();
}

void FLoadingBarrierToken::ReportProgress(float InProgress, const FText& InDescription) {
    if (bReleased) {
        return;
    }
    CurrentProgress = FMath::Clamp(InProgress, 0.0f, 1.0f);
    if (ULoadingSubsystem* Subsystem = SubsystemWeak.Get()) {
        Subsystem->UpdateBarrierProgressInternal(BarrierId, CurrentProgress, InDescription);
    }
}

void FLoadingBarrierToken::Release() {
    if (bReleased) {
        return;
    }
    bReleased = true;

    if (ULoadingSubsystem* Subsystem = SubsystemWeak.Get()) {
        Subsystem->ReleaseLoadingBarrierInternal(BarrierId);
    }
}

FLoadingBarrierToken::FLoadingBarrierToken(
    ULoadingSubsystem* InSubsystem,
    uint64 InBarrierId,
    const FName& InBarrierName) noexcept
    : SubsystemWeak(InSubsystem)
    , BarrierId(InBarrierId)
    , BarrierName(InBarrierName)
    , bReleased(false) {
}
