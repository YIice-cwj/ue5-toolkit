/**
 * @file FGameDebugTimeController.cpp
 * @brief 现代化运行时时间膨胀、慢动作、按世界隔离暂停记忆与逐帧单步调试控制器实现
 */

#include "Time/FGameDebugTimeController.h"

#include "Cheat/FGameCheatRegistry.h"
#include "Config/GameCompileSwitches.h"
#include "Containers/Ticker.h"
#include "Core/GameDebugWorldResolver.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Logging/GameLogCategories.h"
#include "Print/FGameDebugPrint.h"
#include "UObject/ObjectKey.h"

namespace {

inline constexpr bool bEnableTimeController = Game::Config::bEnableGM || Game::Config::bEnableDebugDraw;

inline constexpr const TCHAR* CmdTimeScaleName = TEXT("GM.TimeScale");
inline constexpr const TCHAR* CmdTimePauseName = TEXT("GM.TimePause");
inline constexpr const TCHAR* CmdTimeResumeName = TEXT("GM.TimeResume");
inline constexpr const TCHAR* CmdTimeResetName = TEXT("GM.TimeReset");
inline constexpr const TCHAR* CmdTimeStepName = TEXT("GM.TimeStep");

struct FWorldTimeState {
    TWeakObjectPtr<UWorld> World;
    float PrePauseDilation = 1.0f;
    FTSTicker::FDelegateHandle StepTickerHandle;
    uint64 StepGeneration = 0;
};

TMap<FObjectKey, FWorldTimeState>& GetWorldStates() {
    static TMap<FObjectKey, FWorldTimeState> States;
    return States;
}

void CancelStepTickerForState(FWorldTimeState& State) {
    ++State.StepGeneration;
    if (State.StepTickerHandle.IsValid()) {
        FTSTicker::GetCoreTicker().RemoveTicker(State.StepTickerHandle);
        State.StepTickerHandle.Reset();
    }
}

void PruneStaleWorldStates() {
    TMap<FObjectKey, FWorldTimeState>& States = GetWorldStates();
    for (auto It = States.CreateIterator(); It; ++It) {
        if (!It.Value().World.IsValid()) {
            CancelStepTickerForState(It.Value());
            It.RemoveCurrent();
        }
    }
}

FWorldTimeState& FindOrAddWorldState(UWorld* World) {
    PruneStaleWorldStates();
    const FObjectKey Key(World);
    FWorldTimeState& State = GetWorldStates().FindOrAdd(Key);
    State.World = World;
    return State;
}

bool ResolveWorldAndSettings(const UObject* WorldContextObject, UWorld*& OutWorld, AWorldSettings*& OutWorldSettings) {
    OutWorld = Game::Debug::Private::ResolveWorld(WorldContextObject);
    if (OutWorld == nullptr) {
        OutWorldSettings = nullptr;
        return false;
    }
    OutWorldSettings = OutWorld->GetWorldSettings();
    return OutWorldSettings != nullptr;
}

float ReadWorldDilation(const UWorld* World) {
    if (World == nullptr || World->GetWorldSettings() == nullptr) {
        return 1.0f;
    }
    return World->GetWorldSettings()->TimeDilation;
}

bool TryParseStrictFiniteFloat(const FString& Text, float& OutValue) noexcept {
    if (Text.IsEmpty() || !FCString::IsNumeric(*Text)) {
        return false;
    }
    if (!LexTryParseString(OutValue, *Text) || !FMath::IsFinite(OutValue)) {
        return false;
    }
    return true;
}

bool TryParseStrictPositiveInt32(const FString& Text, int32& OutValue) noexcept {
    const TCHAR* Start = *Text;
    if (Start == nullptr || *Start == TEXT('\0') || FChar::IsWhitespace(*Start)) {
        return false;
    }
    TCHAR* EndPtr = nullptr;
    const int64 Parsed = FCString::Strtoi64(Start, &EndPtr, 10);
    if (EndPtr == Start || EndPtr == nullptr || *EndPtr != TEXT('\0')) {
        return false;
    }
    if (Parsed < 1 || Parsed > static_cast<int64>(MAX_int32)) {
        return false;
    }
    OutValue = static_cast<int32>(Parsed);
    return true;
}

void HandleGmTimeScale(const TArray<FString>& Args) {
    if (Args.IsEmpty()) {
        FGameDebugPrint::PrintWarning(TEXT("用法: GM.TimeScale <倍率> (0.001 ~ 20.0)"));
        return;
    }

    float RequestedScale = 1.0f;
    if (!TryParseStrictFiniteFloat(Args[0], RequestedScale)) {
        FGameDebugPrint::PrintWarning(FString::Printf(TEXT("【时间调试】非法倍率参数: %s"), *Args[0]));
        GAME_LOG(Warning, TEXT("【GameDebug】【Time】GM.TimeScale 收到非法倍率参数: %s"), *Args[0]);
        return;
    }

    if (!FGameDebugTimeController::SetTimeDilation(nullptr, RequestedScale)) {
        FGameDebugPrint::PrintWarning(TEXT("【时间调试】未找到有效世界上下文，设置时间倍率失败"));
        return;
    }

    const float AppliedScale = FGameDebugTimeController::GetTimeDilation(nullptr);
    FGameDebugPrint::Print(
        FString::Printf(TEXT("【时间调试】全局时间膨胀率设置为: %.4f x"), AppliedScale),
        FColor::Green,
        3.0f
    );
}

void HandleGmTimePause([[maybe_unused]] const TArray<FString>& Args) {
    if (!FGameDebugTimeController::Pause(nullptr)) {
        FGameDebugPrint::PrintWarning(TEXT("【时间调试】未找到有效世界上下文，暂停失败"));
        return;
    }
    FGameDebugPrint::Print(TEXT("【时间调试】游戏世界时钟已暂停冻结"), FColor::Yellow, 3.0f);
}

void HandleGmTimeResume([[maybe_unused]] const TArray<FString>& Args) {
    if (!FGameDebugTimeController::Resume(nullptr)) {
        FGameDebugPrint::PrintWarning(TEXT("【时间调试】未找到有效世界上下文，恢复失败"));
        return;
    }
    const float AppliedScale = FGameDebugTimeController::GetTimeDilation(nullptr);
    FGameDebugPrint::Print(
        FString::Printf(TEXT("【时间调试】游戏世界时钟已恢复至: %.4f x"), AppliedScale),
        FColor::Green,
        3.0f
    );
}

void HandleGmTimeReset([[maybe_unused]] const TArray<FString>& Args) {
    if (!FGameDebugTimeController::ResetTime(nullptr)) {
        FGameDebugPrint::PrintWarning(TEXT("【时间调试】未找到有效世界上下文，重置失败"));
        return;
    }
    FGameDebugPrint::Print(TEXT("【时间调试】游戏世界时钟已重置为 1.0x"), FColor::Green, 3.0f);
}

void HandleGmTimeStep(const TArray<FString>& Args) {
    int32 Frames = 1;
    if (!Args.IsEmpty()) {
        if (!TryParseStrictPositiveInt32(Args[0], Frames)) {
            FGameDebugPrint::PrintWarning(FString::Printf(TEXT("【时间调试】非法步进帧数参数: %s（须为正整数）"), *Args[0]));
            GAME_LOG(Warning, TEXT("【GameDebug】【Time】GM.TimeStep 收到非法帧数参数: %s"), *Args[0]);
            return;
        }
    }

    if (!FGameDebugTimeController::StepFrame(nullptr, Frames)) {
        FGameDebugPrint::PrintWarning(TEXT("【时间调试】未找到有效世界上下文，单步推进失败"));
        return;
    }

    FGameDebugPrint::Print(
        FString::Printf(TEXT("【时间调试】已单步推进 %d 帧"), Frames),
        FColor::Cyan,
        2.0f
    );
}

}  // namespace

void FGameDebugTimeController::Initialize() {
    if constexpr (!Game::Config::bEnableGM) {
        return;
    }

    FGameCheatRegistry& Registry = FGameCheatRegistry::Get();
    const FName CmdScale(CmdTimeScaleName);
    const FName CmdPause(CmdTimePauseName);
    const FName CmdResume(CmdTimeResumeName);
    const FName CmdReset(CmdTimeResetName);
    const FName CmdStep(CmdTimeStepName);

    if (!Registry.HasCheat(CmdScale)) {
        Registry.RegisterCheat(
            CmdScale,
            TEXT("设置全局游戏时钟倍率。用法: GM.TimeScale <倍率> (0.001 ~ 20.0)"),
            &HandleGmTimeScale
        );
    }
    if (!Registry.HasCheat(CmdPause)) {
        Registry.RegisterCheat(
            CmdPause,
            TEXT("冻结游戏世界时钟。用法: GM.TimePause"),
            &HandleGmTimePause
        );
    }
    if (!Registry.HasCheat(CmdResume)) {
        Registry.RegisterCheat(
            CmdResume,
            TEXT("恢复游戏世界时钟至暂停前速度。用法: GM.TimeResume"),
            &HandleGmTimeResume
        );
    }
    if (!Registry.HasCheat(CmdReset)) {
        Registry.RegisterCheat(
            CmdReset,
            TEXT("重置游戏世界时钟为 1.0 标准速度。用法: GM.TimeReset"),
            &HandleGmTimeReset
        );
    }
    if (!Registry.HasCheat(CmdStep)) {
        Registry.RegisterCheat(
            CmdStep,
            TEXT("逐帧单步推进世界时钟。用法: GM.TimeStep [帧数(默认1)]"),
            &HandleGmTimeStep
        );
    }
}

void FGameDebugTimeController::Shutdown() {
    if constexpr (bEnableTimeController) {
        TMap<FObjectKey, FWorldTimeState>& States = GetWorldStates();
        for (TPair<FObjectKey, FWorldTimeState>& Pair : States) {
            CancelStepTickerForState(Pair.Value);
        }
        States.Empty();
    }

    if constexpr (Game::Config::bEnableGM) {
        FGameCheatRegistry& Registry = FGameCheatRegistry::Get();
        Registry.UnregisterCheat(FName(CmdTimeScaleName));
        Registry.UnregisterCheat(FName(CmdTimePauseName));
        Registry.UnregisterCheat(FName(CmdTimeResumeName));
        Registry.UnregisterCheat(FName(CmdTimeResetName));
        Registry.UnregisterCheat(FName(CmdTimeStepName));
    }
}

bool FGameDebugTimeController::SetTimeDilation(const UObject* WorldContextObject, float TimeDilation) {
    if constexpr (!bEnableTimeController) {
        return false;
    }

    if (!FMath::IsFinite(TimeDilation)) {
        GAME_LOG(Warning, TEXT("【GameDebug】【Time】拒绝设置非有限时间倍率（NaN/Inf）。"));
        return false;
    }

    UWorld* TargetWorld = nullptr;
    AWorldSettings* WorldSettings = nullptr;
    if (!ResolveWorldAndSettings(WorldContextObject, TargetWorld, WorldSettings)) {
        GAME_LOG(Warning, TEXT("【GameDebug】【Time】SetTimeDilation 未解析到有效 UWorld/WorldSettings。"));
        return false;
    }

    FWorldTimeState& State = FindOrAddWorldState(TargetWorld);
    CancelStepTickerForState(State);

    const float ClampedScale = FMath::Clamp(TimeDilation, MinTimeDilation, MaxTimeDilation);
    const float AppliedScale = WorldSettings->SetTimeDilation(ClampedScale);
    State.PrePauseDilation = AppliedScale;
    GAME_LOG(Verbose, TEXT("【GameDebug】【Time】世界 [%s] 时间膨胀率设为 %.4fx。"), *TargetWorld->GetName(), AppliedScale);
    return true;
}

float FGameDebugTimeController::GetTimeDilation(const UObject* WorldContextObject) {
    if constexpr (!bEnableTimeController) {
        return 1.0f;
    }

    const UWorld* TargetWorld = Game::Debug::Private::ResolveWorld(WorldContextObject);
    return ReadWorldDilation(TargetWorld);
}

bool FGameDebugTimeController::Pause(const UObject* WorldContextObject) {
    if constexpr (!bEnableTimeController) {
        return false;
    }

    UWorld* TargetWorld = nullptr;
    AWorldSettings* WorldSettings = nullptr;
    if (!ResolveWorldAndSettings(WorldContextObject, TargetWorld, WorldSettings)) {
        GAME_LOG(Warning, TEXT("【GameDebug】【Time】Pause 未解析到有效 UWorld/WorldSettings。"));
        return false;
    }

    FWorldTimeState& State = FindOrAddWorldState(TargetWorld);
    CancelStepTickerForState(State);

    const float CurrentDilation = WorldSettings->TimeDilation;
    if (CurrentDilation > PauseDetectionThreshold) {
        State.PrePauseDilation = FMath::Clamp(CurrentDilation, MinTimeDilation, MaxTimeDilation);
    }

    const float AppliedScale = WorldSettings->SetTimeDilation(PausedTimeDilation);
    if (AppliedScale > PauseDetectionThreshold) {
        GAME_LOG(
            Warning,
            TEXT("【GameDebug】【Time】世界 [%s] 的 WorldSettings::MinGlobalTimeDilation 限制导致无法降至暂停阈值以下（实际 %.4fx）。"),
            *TargetWorld->GetName(),
            AppliedScale
        );
        return false;
    }
    GAME_LOG(Verbose, TEXT("【GameDebug】【Time】世界 [%s] 已暂停（记忆倍率 %.4fx）。"), *TargetWorld->GetName(), State.PrePauseDilation);
    return true;
}

bool FGameDebugTimeController::Resume(const UObject* WorldContextObject) {
    if constexpr (!bEnableTimeController) {
        return false;
    }

    UWorld* TargetWorld = nullptr;
    AWorldSettings* WorldSettings = nullptr;
    if (!ResolveWorldAndSettings(WorldContextObject, TargetWorld, WorldSettings)) {
        GAME_LOG(Warning, TEXT("【GameDebug】【Time】Resume 未解析到有效 UWorld/WorldSettings。"));
        return false;
    }

    FWorldTimeState& State = FindOrAddWorldState(TargetWorld);
    CancelStepTickerForState(State);

    const float TargetScale = (State.PrePauseDilation >= MinTimeDilation) ? State.PrePauseDilation : 1.0f;
    const float AppliedScale = WorldSettings->SetTimeDilation(TargetScale);
    GAME_LOG(Verbose, TEXT("【GameDebug】【Time】世界 [%s] 已恢复至 %.4fx。"), *TargetWorld->GetName(), AppliedScale);
    return true;
}

bool FGameDebugTimeController::IsPaused(const UObject* WorldContextObject) {
    if constexpr (!bEnableTimeController) {
        return false;
    }

    const UWorld* TargetWorld = Game::Debug::Private::ResolveWorld(WorldContextObject);
    if (TargetWorld == nullptr) {
        return false;
    }

    return ReadWorldDilation(TargetWorld) <= PauseDetectionThreshold;
}

bool FGameDebugTimeController::ResetTime(const UObject* WorldContextObject) {
    if constexpr (!bEnableTimeController) {
        return false;
    }

    UWorld* TargetWorld = nullptr;
    AWorldSettings* WorldSettings = nullptr;
    if (!ResolveWorldAndSettings(WorldContextObject, TargetWorld, WorldSettings)) {
        GAME_LOG(Warning, TEXT("【GameDebug】【Time】ResetTime 未解析到有效 UWorld/WorldSettings。"));
        return false;
    }

    FWorldTimeState& State = FindOrAddWorldState(TargetWorld);
    CancelStepTickerForState(State);
    State.PrePauseDilation = 1.0f;
    WorldSettings->SetTimeDilation(1.0f);
    GAME_LOG(Verbose, TEXT("【GameDebug】【Time】世界 [%s] 时间倍率已重置为 1.0x。"), *TargetWorld->GetName());
    return true;
}

bool FGameDebugTimeController::StepFrame(const UObject* WorldContextObject, int32 Frames) {
    if constexpr (!bEnableTimeController) {
        return false;
    }

    UWorld* TargetWorld = nullptr;
    AWorldSettings* WorldSettings = nullptr;
    if (!ResolveWorldAndSettings(WorldContextObject, TargetWorld, WorldSettings)) {
        GAME_LOG(Warning, TEXT("【GameDebug】【Time】StepFrame 未解析到有效 UWorld/WorldSettings。"));
        return false;
    }

    FWorldTimeState& State = FindOrAddWorldState(TargetWorld);
    CancelStepTickerForState(State);

    const float CurrentDilation = WorldSettings->TimeDilation;
    if (CurrentDilation > PauseDetectionThreshold) {
        State.PrePauseDilation = FMath::Clamp(CurrentDilation, MinTimeDilation, MaxTimeDilation);
    }

    const float StepDilation = (State.PrePauseDilation >= MinTimeDilation) ? State.PrePauseDilation : 1.0f;
    WorldSettings->SetTimeDilation(StepDilation);

    const FObjectKey WorldKey(TargetWorld);
    const uint64 ExpectedGeneration = State.StepGeneration;
    const TWeakObjectPtr<UWorld> WorldWeak(TargetWorld);
    const int32 TotalFrames = FMath::Max(1, Frames);
    const int64 EffectiveTicks = static_cast<int64>(TotalFrames) + (TargetWorld->bInTick ? 1LL : 0LL);
    TSharedRef<int64> FramesRemaining = MakeShared<int64>(EffectiveTicks);

    State.StepTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([WorldKey, ExpectedGeneration, WorldWeak, FramesRemaining](float) -> bool {
            UWorld* ValidWorld = WorldWeak.Get();
            if (ValidWorld == nullptr) {
                if (const FWorldTimeState* ActiveState = GetWorldStates().Find(WorldKey);
                    ActiveState != nullptr && ActiveState->StepGeneration == ExpectedGeneration) {
                    GetWorldStates().Remove(WorldKey);
                }
                return false;
            }

            --(*FramesRemaining);
            if (*FramesRemaining > 0) {
                return true;
            }

            if (AWorldSettings* ValidSettings = ValidWorld->GetWorldSettings()) {
                ValidSettings->SetTimeDilation(PausedTimeDilation);
            }
            if (FWorldTimeState* ActiveState = GetWorldStates().Find(WorldKey);
                ActiveState != nullptr && ActiveState->StepGeneration == ExpectedGeneration) {
                ActiveState->StepTickerHandle.Reset();
            }
            return false;
        })
    );
    return true;
}
