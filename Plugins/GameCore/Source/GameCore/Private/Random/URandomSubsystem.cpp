/**
 * @file URandomSubsystem.cpp
 * @brief URandomSubsystem 世界级多通道隔离随机数子系统薄壳实现
 */

#include "Random/URandomSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

void URandomSubsystem::Deinitialize() {
    Manager.ClearAllStreams();
    Super::Deinitialize();
}

FGameRandomStream& URandomSubsystem::GetOrCreateStream(const FName& ChannelName) {
    return Manager.GetOrCreateStream(ChannelName);
}

FGameRandomStream& URandomSubsystem::InitializeStream(const FName& ChannelName, uint64 Seed) {
    return Manager.InitializeStream(ChannelName, Seed);
}

bool URandomSubsystem::HasStream(const FName& ChannelName) const {
    return Manager.HasStream(ChannelName);
}

bool URandomSubsystem::RemoveStream(const FName& ChannelName) {
    return Manager.RemoveStream(ChannelName);
}

void URandomSubsystem::ClearAllStreams() {
    Manager.ClearAllStreams();
}

void URandomSubsystem::ExportStreams(FArchive& Ar) const {
    Manager.ExportStreams(Ar);
}

bool URandomSubsystem::ImportStreams(FArchive& Ar) {
    return Manager.ImportStreams(Ar);
}

URandomSubsystem& URandomSubsystem::Get(const UObject* WorldContextObject) {
    URandomSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

URandomSubsystem* URandomSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr || GEngine == nullptr) {
        return nullptr;
    }
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
        return World->GetSubsystem<URandomSubsystem>();
    }
    return nullptr;
}
