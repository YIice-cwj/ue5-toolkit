/**
 * @file GameGameplayCoreTestUtils.h
 * @brief GameGameplayCore 自动化测试共享世界解析与 RAII 宿主 Actor 辅助工具
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace Game::GameplayCore::Test {

/**
 * @brief 获取当前自动化测试可用的有效世界上下文指针
 * @return 第一个非空世界指针；无可用世界时返回 nullptr
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
 * @brief 在指定世界中生成默认自动化测试宿主 Actor
 * @param InWorld 目标世界指针
 * @return 新生成的宿主 Actor 指针；世界为空时返回 nullptr
 */
[[nodiscard]] inline AActor* SpawnTestHost(UWorld* InWorld) {
    if (InWorld == nullptr) {
        return nullptr;
    }
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return InWorld->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
}

/**
 * @brief RAII 自动化测试宿主 Actor 守卫（析构自动销毁，避免异常早退泄漏场景对象）
 */
class FScopedTestHostActor {
public:
    FScopedTestHostActor()
        : HostActor(SpawnTestHost(GetTestWorld())) {
    }

    ~FScopedTestHostActor() {
        if (AActor* Actor = HostActor.Get()) {
            Actor->Destroy();
        }
    }

    FScopedTestHostActor(const FScopedTestHostActor&) = delete;
    FScopedTestHostActor& operator=(const FScopedTestHostActor&) = delete;
    FScopedTestHostActor(FScopedTestHostActor&&) = delete;
    FScopedTestHostActor& operator=(FScopedTestHostActor&&) = delete;

    /**
     * @brief 检查宿主 Actor 是否创建成功且有效
     * @return 有效返回 true
     */
    [[nodiscard]] bool IsValidHost() const noexcept {
        return HostActor.IsValid();
    }

    /**
     * @brief 获取底层宿主 Actor 指针
     * @return 宿主 Actor 指针
     */
    [[nodiscard]] AActor* Get() const noexcept {
        return HostActor.Get();
    }

private:
    TWeakObjectPtr<AActor> HostActor;
};

}  // namespace Game::GameplayCore::Test
