/**
 * @file FGameDebugTimeController.cpp
 * @brief 现代化运行时时间膨胀、慢动作、暂停与逐帧单步调试控制器实现
 */

#include "Time/FGameDebugTimeController.h"

#include "Cheat/FGameCheatRegistry.h"
#include "Config/GameCompileSwitches.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Print/FGameDebugPrint.h"

namespace {

FTSTicker::FDelegateHandle StepTickerHandle;

void CancelPendingStepTicker() {
    if (StepTickerHandle.IsValid()) {
        FTSTicker::GetCoreTicker().RemoveTicker(StepTickerHandle);
        StepTickerHandle.Reset();
    }
}

}  // namespace

void FGameDebugTimeController::Initialize() {
    if constexpr (Game::Config::bEnableGM) {
        FGameCheatRegistry& Registry = FGameCheatRegistry::Get();

        Registry.RegisterCheat(
            FName(TEXT("GM.TimeScale")),
            TEXT("设置全局游戏时钟倍率。用法: GM.TimeScale <倍率> (如 0.1 慢动作, 1.0 正常, 2.0 快进)"),
            [](const TArray<FString>& Args) {
                if (Args.IsEmpty()) {
                    FGameDebugPrint::PrintWarning(TEXT("用法: GM.TimeScale <倍率>"));
                    return;
                }
                float Scale = 1.0f;
                if (LexTryParseString(Scale, *Args[0])) {
                    SetTimeDilation(nullptr, Scale);
                    FGameDebugPrint::Print(
                        FString::Printf(TEXT("【时间调试】全局时间膨胀率设置为: %.2f x"), Scale),
                        FColor::Green,
                        3.0f
                    );
                }
            }
        );

        Registry.RegisterCheat(
            FName(TEXT("GM.TimePause")),
            TEXT("冻结游戏世界时钟。用法: GM.TimePause"),
            [](const TArray<FString>&) {
                Pause(nullptr);
                FGameDebugPrint::Print(TEXT("【时间调试】游戏世界时钟已暂停冻结"), FColor::Yellow, 3.0f);
            }
        );

        Registry.RegisterCheat(
            FName(TEXT("GM.TimeResume")),
            TEXT("恢复游戏世界时钟至暂停前速度。用法: GM.TimeResume"),
            [](const TArray<FString>&) {
                Resume(nullptr);
                FGameDebugPrint::Print(
                    FString::Printf(TEXT("【时间调试】游戏世界时钟已恢复至: %.2f x"), PrePauseTimeDilation),
                    FColor::Green,
                    3.0f
                );
            }
        );

        Registry.RegisterCheat(
            FName(TEXT("GM.TimeReset")),
            TEXT("重置游戏世界时钟为 1.0 标准速度。用法: GM.TimeReset"),
            [](const TArray<FString>&) {
                ResetTime(nullptr);
                FGameDebugPrint::Print(TEXT("【时间调试】游戏世界时钟已重置为 1.0x"), FColor::Green, 3.0f);
            }
        );

        Registry.RegisterCheat(
            FName(TEXT("GM.TimeStep")),
            TEXT("逐帧单步推进世界时钟。用法: GM.TimeStep [帧数(默认1)]"),
            [](const TArray<FString>& Args) {
                int32 Frames = 1;
                if (!Args.IsEmpty()) {
                    LexTryParseString(Frames, *Args[0]);
                }
                StepFrame(nullptr, Frames);
                FGameDebugPrint::Print(
                    FString::Printf(TEXT("【时间调试】已单步推进 %d 帧"), FMath::Max(1, Frames)),
                    FColor::Cyan,
                    2.0f
                );
            }
        );
    }
}

UWorld* FGameDebugTimeController::ResolveWorld(const UObject* WorldContextObject) {
    if (WorldContextObject != nullptr) {
        if (const UWorld* AsWorld = Cast<UWorld>(WorldContextObject)) {
            return const_cast<UWorld*>(AsWorld);
        }
        if (GEngine != nullptr) {
            if (UWorld* CtxWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
                return CtxWorld;
            }
        }
    }

    if (GEngine != nullptr) {
        for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
            if (Context.World() != nullptr && Context.World()->IsGameWorld()) {
                return Context.World();
            }
        }
        if (!GEngine->GetWorldContexts().IsEmpty()) {
            return GEngine->GetWorldContexts()[0].World();
        }
    }
    return nullptr;
}

bool FGameDebugTimeController::SetTimeDilation(const UObject* WorldContextObject, float TimeDilation) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return false;
    } else {
        UWorld* TargetWorld = ResolveWorld(WorldContextObject);
        if (TargetWorld == nullptr) {
            return false;
        }

        CancelPendingStepTicker();

        const float ClampedScale = FMath::Clamp(TimeDilation, 0.0001f, 20.0f);
        if (ClampedScale > 0.001f) {
            PrePauseTimeDilation = ClampedScale;
        }
        UGameplayStatics::SetGlobalTimeDilation(TargetWorld, ClampedScale);
        return true;
    }
}

float FGameDebugTimeController::GetTimeDilation(const UObject* WorldContextObject) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return 1.0f;
    } else {
        UWorld* TargetWorld = ResolveWorld(WorldContextObject);
        if (TargetWorld == nullptr || TargetWorld->GetWorldSettings() == nullptr) {
            return 1.0f;
        }
        return TargetWorld->GetWorldSettings()->TimeDilation;
    }
}

bool FGameDebugTimeController::Pause(const UObject* WorldContextObject) {
    const float CurrentDilation = GetTimeDilation(WorldContextObject);
    if (CurrentDilation > 0.001f) {
        PrePauseTimeDilation = CurrentDilation;
    }
    return SetTimeDilation(WorldContextObject, 0.0001f);
}

bool FGameDebugTimeController::Resume(const UObject* WorldContextObject) {
    const float TargetScale = (PrePauseTimeDilation > 0.001f) ? PrePauseTimeDilation : 1.0f;
    return SetTimeDilation(WorldContextObject, TargetScale);
}

bool FGameDebugTimeController::IsPaused(const UObject* WorldContextObject) {
    return GetTimeDilation(WorldContextObject) <= 0.001f;
}

bool FGameDebugTimeController::ResetTime(const UObject* WorldContextObject) {
    PrePauseTimeDilation = 1.0f;
    return SetTimeDilation(WorldContextObject, 1.0f);
}

bool FGameDebugTimeController::StepFrame(const UObject* WorldContextObject, int32 Frames) {
    if constexpr (!Game::Config::bEnableDebugDraw) {
        return false;
    } else {
        UWorld* TargetWorld = ResolveWorld(WorldContextObject);
        if (TargetWorld == nullptr) {
            return false;
        }

        CancelPendingStepTicker();

        const float StepDilation = (PrePauseTimeDilation > 0.001f) ? PrePauseTimeDilation : 1.0f;
        UGameplayStatics::SetGlobalTimeDilation(TargetWorld, StepDilation);

        TWeakObjectPtr<UWorld> WorldWeak(TargetWorld);
        const int32 TotalFrames = FMath::Max(1, Frames);
        TSharedRef<int32> FramesRemaining = MakeShared<int32>(TotalFrames);

        StepTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([WorldWeak, FramesRemaining](float) -> bool {
                --(*FramesRemaining);
                if (*FramesRemaining <= 0) {
                    if (UWorld* ValidWorld = WorldWeak.Get()) {
                        UGameplayStatics::SetGlobalTimeDilation(ValidWorld, 0.0001f);
                    }
                    StepTickerHandle.Reset();
                    return false;
                }
                return true;
            })
        );
        return true;
    }
}
