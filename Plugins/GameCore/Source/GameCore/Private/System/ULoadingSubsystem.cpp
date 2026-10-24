/**
 * @file ULoadingSubsystem.cpp
 * @brief 通用关卡流转与加载幕布子系统实现
 */

#include "System/ULoadingSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
DEFINE_LOG_CATEGORY_STATIC(LogGameLoading, Log, All);

ULoadingSubsystem* ULoadingSubsystem::Get(const UObject* WorldContextObject) {
    if (const UGameInstance* AsGI = Cast<UGameInstance>(WorldContextObject)) {
        return AsGI->GetSubsystem<ULoadingSubsystem>();
    }

    const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (World == nullptr) {
        return nullptr;
    }

    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<ULoadingSubsystem>() : nullptr;
}

void ULoadingSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &ULoadingSubsystem::OnPreLoadMap);
    FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ULoadingSubsystem::OnPostLoadMap);
    UE_LOG(LogGameLoading, Log, TEXT("【加载子系统】已挂钩引擎地图加载生命周期委托。"));
}

void ULoadingSubsystem::Deinitialize() {
    FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
    FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
    UE_LOG(LogGameLoading, Log, TEXT("【加载子系统】已注销引擎地图加载生命周期委托。"));
    Super::Deinitialize();
}

void ULoadingSubsystem::OpenLevelWithLoading(const FName& LevelName) {
    if (bIsLoading) {
        UE_LOG(LogGameLoading, Warning, TEXT("【加载子系统】OpenLevelWithLoading 被重复调用，当前仍处于加载中，忽略本次请求。"));
        return;
    }
    if (LevelName.IsNone()) {
        UE_LOG(LogGameLoading, Warning, TEXT("【加载子系统】OpenLevelWithLoading 执行失败：目标关卡名称为空 (None)。"));
        return;
    }

    UGameInstance* GameInstance = GetGameInstance();
    if (GameInstance == nullptr || GameInstance->GetWorld() == nullptr) {
        UE_LOG(LogGameLoading, Warning, TEXT("【加载子系统】OpenLevelWithLoading 执行失败：GameInstance 或世界上下文无效，未置位加载态。"));
        return;
    }

    bIsLoading = true;
    TargetLevelName = LevelName;
    OnLoadingStateChanged.Broadcast(true, TargetLevelName);

    UGameplayStatics::OpenLevel(GameInstance, LevelName);
}

bool ULoadingSubsystem::IsLoading() const {
    return bIsLoading;
}

FName ULoadingSubsystem::GetTargetLevelName() const {
    return TargetLevelName;
}

void ULoadingSubsystem::OnPreLoadMap(const FString& MapName) {
    if (!bIsLoading) {
        bIsLoading = true;
        TargetLevelName = FName(*MapName);
        OnLoadingStateChanged.Broadcast(true, TargetLevelName);
        UE_LOG(LogGameLoading, Log, TEXT("【加载子系统】引擎触发 PreLoadMap，即将开始载入地图: %s"), *MapName);
    }
}

void ULoadingSubsystem::OnPostLoadMap(UWorld* LoadedWorld) {
    if (LoadedWorld != nullptr && LoadedWorld->GetGameInstance() != nullptr && LoadedWorld->GetGameInstance() != GetGameInstance()) {
        return;
    }

    if (bIsLoading) {
        bIsLoading = false;
        const FName CompletedLevelName = TargetLevelName;
        TargetLevelName = NAME_None;
        OnLoadingStateChanged.Broadcast(false, CompletedLevelName);
        UE_LOG(LogGameLoading, Log, TEXT("【加载子系统】引擎完成 PostLoadMap，目标关卡 [%s] 加载就绪。"), *CompletedLevelName.ToString());
    }
}
