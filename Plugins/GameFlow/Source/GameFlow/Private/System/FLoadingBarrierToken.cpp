/**
 * @file FLoadingBarrierToken.cpp
 * @brief 关卡加载幕布异步就绪屏障 RAII 令牌实现
 */

#include "System/FLoadingBarrierToken.h"

#include "System/ULoadingSubsystem.h"

FLoadingBarrierToken::~FLoadingBarrierToken() {
    Release();
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
    , BarrierName(InBarrierName) {
}
