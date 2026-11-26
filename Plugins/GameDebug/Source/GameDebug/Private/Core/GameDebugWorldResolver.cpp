/**
 * @file GameDebugWorldResolver.cpp
 * @brief GameDebug 模块内部世界上下文安全解析工具实现
 */

#include "Core/GameDebugWorldResolver.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

namespace Game::Debug::Private {

UWorld* ResolveWorld(const UObject* WorldContextObject) {
    if (WorldContextObject != nullptr) {
        if (!IsValid(WorldContextObject)) {
            return nullptr;
        }
        if (const UWorld* AsWorld = Cast<UWorld>(WorldContextObject)) {
            return const_cast<UWorld*>(AsWorld);
        }
        if (GEngine == nullptr) {
            return nullptr;
        }
        return GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    }

    if (GEngine == nullptr) {
        return nullptr;
    }

    UWorld* FallbackWorld = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        UWorld* Candidate = Context.World();
        if (Candidate == nullptr) {
            continue;
        }
        if (Candidate->IsGameWorld()) {
            return Candidate;
        }
        if (FallbackWorld == nullptr) {
            FallbackWorld = Candidate;
        }
    }
    return FallbackWorld;
}

}  // namespace Game::Debug::Private
