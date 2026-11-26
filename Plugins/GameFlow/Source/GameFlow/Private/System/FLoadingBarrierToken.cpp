/**
 * @file FLoadingBarrierToken.cpp
 * @brief 关卡加载幕布异步就绪屏障零堆分配 RAII 令牌实现
 */

#include "System/FLoadingBarrierToken.h"

#include "System/ULoadingSubsystem.h"

FLoadingBarrierToken::FLoadingBarrierToken(FLoadingBarrierToken&& Other) noexcept {
    TakeOwnershipFrom(MoveTemp(Other));
}

FLoadingBarrierToken& FLoadingBarrierToken::operator=(FLoadingBarrierToken&& Other) noexcept {
    if (this != &Other) {
        Release();
        TakeOwnershipFrom(MoveTemp(Other));
    }
    return *this;
}

FLoadingBarrierToken::~FLoadingBarrierToken() {
    Release();
}

void FLoadingBarrierToken::Release() {
    if (bReleased) {
        return;
    }
    bReleased = true;

    const FLoadingBarrierHandle HandleToRelease = BarrierHandle;
    BarrierHandle.Reset();

    if (ULoadingSubsystem* Subsystem = SubsystemWeak.Get()) {
        Subsystem->ReleaseLoadingBarrierInternal(HandleToRelease);
    }
    SubsystemWeak.Reset();
}

void FLoadingBarrierToken::ReportProgress(float InProgress, const FText& InDescription) {
    if (bReleased) {
        return;
    }
    if (ULoadingSubsystem* Subsystem = SubsystemWeak.Get()) {
        float ClampedProgress = 0.0f;
        if (Subsystem->UpdateBarrierProgressInternal(BarrierHandle, InProgress, InDescription, ClampedProgress)) {
            CurrentProgress = ClampedProgress;
        }
    }
}

bool FLoadingBarrierToken::IsValid() const noexcept {
    if (bReleased) {
        return false;
    }
    const ULoadingSubsystem* Subsystem = SubsystemWeak.Get();
    return Subsystem != nullptr && Subsystem->IsBarrierActiveInternal(BarrierHandle);
}

FLoadingBarrierToken::FLoadingBarrierToken(
    ULoadingSubsystem* InSubsystem,
    FLoadingBarrierHandle InBarrierHandle,
    const FName& InBarrierName
) noexcept
    : SubsystemWeak(InSubsystem)
    , BarrierHandle(InBarrierHandle)
    , BarrierName(InBarrierName)
    , bReleased(false) {
}

void FLoadingBarrierToken::TakeOwnershipFrom(FLoadingBarrierToken&& Other) noexcept {
    SubsystemWeak = Other.SubsystemWeak;
    BarrierHandle = Other.BarrierHandle;
    BarrierName = Other.BarrierName;
    CurrentProgress = Other.CurrentProgress;
    bReleased = Other.bReleased;

    Other.SubsystemWeak.Reset();
    Other.BarrierHandle.Reset();
    Other.BarrierName = NAME_None;
    Other.CurrentProgress = 0.0f;
    Other.bReleased = true;
}
