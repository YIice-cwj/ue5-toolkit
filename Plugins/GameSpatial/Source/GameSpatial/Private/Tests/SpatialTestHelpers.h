/**
 * @file SpatialTestHelpers.h
 * @brief GameSpatial 自动化测试与微基准共享世界上下文及测试 Actor 构造辅助原语
 */

#pragma once

#include "CoreMinimal.h"

#if WITH_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace Game::Spatial::TestHelpers {

    /**
     * @brief 获取当前自动化测试可用的首个有效世界上下文指针
     * @return 有效 UWorld 指针，无可用世界上下文返回 nullptr
     */
    [[nodiscard]] inline UWorld* GetTestWorld() {
        if (GEngine == nullptr) {
            return nullptr;
        }
        for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
            if (Context.World() != nullptr) {
                return Context.World();
            }
        }
        return nullptr;
    }

    /**
     * @brief 在指定世界生成挂载 USceneComponent 根组件的轻量测试 Actor 并定位到目标坐标
     * @param World 目标世界上下文
     * @param Location 初始三维世界坐标
     * @return 生成的 Actor 指针，失败返回 nullptr
     */
    [[nodiscard]] inline AActor* SpawnTestActor(UWorld* World, const FVector& Location) {
        if (World == nullptr) {
            return nullptr;
        }
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), SpawnParams);
        if (Actor != nullptr) {
            USceneComponent* RootComp = NewObject<USceneComponent>(Actor);
            Actor->SetRootComponent(RootComp);
            RootComp->RegisterComponent();
            Actor->SetActorLocation(Location);
        }
        return Actor;
    }

}  // namespace Game::Spatial::TestHelpers

#endif
