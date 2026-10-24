/**
 * @file UObservableRegistrySubsystem.cpp
 * @brief World 作用域的可观察值注册表子系统实现
 */

#include "System/UObservableRegistrySubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

UObservableRegistrySubsystem* UObservableRegistrySubsystem::Get(const UObject* WorldContextObject) {
    const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    return World != nullptr ? World->GetSubsystem<UObservableRegistrySubsystem>() : nullptr;
}

void UObservableRegistrySubsystem::Deinitialize() {
    Registry.Reset();
    Super::Deinitialize();
}

Game::Core::FObservableRegistry& UObservableRegistrySubsystem::GetRegistry() {
    return Registry;
}

const Game::Core::FObservableRegistry& UObservableRegistrySubsystem::GetRegistry() const {
    return Registry;
}
