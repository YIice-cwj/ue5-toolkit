/**
 * @file GameFlowTestUtils.h
 * @brief GameFlow 自动化测试公共 RAII 独立世界与地图加载上下文模拟辅助工具（私有内部测试头）
 */

#pragma once

#if WITH_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "System/ULoadingSubsystem.h"
#include "UObject/UObjectGlobals.h"

namespace Game::Flow::Test {

/**
 * @brief 自动创建与析构关停独立测试 GameInstance 的 RAII 守卫（防提前退出泄漏实例）
 */
class FScopedTestLoadingWorld {
public:
    /**
     * @brief 构造并初始化独立的测试 GameInstance 与 ULoadingSubsystem
     * @param InWorldName 测试世界名称
     */
    explicit FScopedTestLoadingWorld(const FName& InWorldName) {
        if (GEngine == nullptr) {
            return;
        }
        GameInstance = NewObject<UGameInstance>(GEngine);
        if (GameInstance != nullptr) {
            GameInstance->InitializeStandalone(InWorldName);
            Subsystem = GameInstance->GetSubsystem<ULoadingSubsystem>();
        }
    }

    FScopedTestLoadingWorld(const FScopedTestLoadingWorld&) = delete;
    FScopedTestLoadingWorld& operator=(const FScopedTestLoadingWorld&) = delete;
    FScopedTestLoadingWorld(FScopedTestLoadingWorld&&) = delete;
    FScopedTestLoadingWorld& operator=(FScopedTestLoadingWorld&&) = delete;

    /**
     * @brief 析构时若尚未显式关停则自动执行 GameInstance::Shutdown()
     */
    ~FScopedTestLoadingWorld() {
        ShutdownNow();
    }

    /**
     * @brief 主动关停测试 GameInstance（幂等安全，供测试 Deinitialize 行为时显式调用）
     */
    void ShutdownNow() {
        if (GameInstance != nullptr) {
            UGameInstance* ToShutdown = GameInstance;
            GameInstance = nullptr;
            Subsystem = nullptr;
            ToShutdown->Shutdown();
        }
    }

    /**
     * @brief 检查当前测试世界、实例与加载子系统是否全部有效
     * @return 全部有效返回 true
     */
    [[nodiscard]] bool IsValid() const noexcept {
        return GameInstance != nullptr && Subsystem != nullptr && GameInstance->GetWorld() != nullptr;
    }

    /**
     * @brief 获取测试 GameInstance 指针
     * @return GameInstance 指针
     */
    [[nodiscard]] UGameInstance* GetGameInstance() const noexcept {
        return GameInstance;
    }

    /**
     * @brief 获取测试世界指针
     * @return UWorld 指针
     */
    [[nodiscard]] UWorld* GetWorld() const noexcept {
        return GameInstance != nullptr ? GameInstance->GetWorld() : nullptr;
    }

    /**
     * @brief 获取关联加载子系统指针
     * @return ULoadingSubsystem 指针
     */
    [[nodiscard]] ULoadingSubsystem* GetSubsystem() const noexcept {
        return Subsystem;
    }

    /**
     * @brief 获取当前测试世界对应的引擎 FWorldContext
     * @return 世界上下文指针，不存在返回 nullptr
     */
    [[nodiscard]] FWorldContext* GetWorldContext() const {
        UWorld* World = GetWorld();
        return (GEngine != nullptr && World != nullptr) ? GEngine->GetWorldContextFromWorld(World) : nullptr;
    }

    /**
     * @brief 广播绑定当前测试实例上下文的 PreLoadMapWithContext 委托
     * @param MapName 模拟目标地图路径
     * @return 成功找到上下文并完成广播返回 true
     */
    bool BroadcastPreLoadMap(const FString& MapName) const {
        if (FWorldContext* Context = GetWorldContext()) {
            FCoreUObjectDelegates::PreLoadMapWithContext.Broadcast(*Context, MapName);
            return true;
        }
        return false;
    }

    /**
     * @brief 广播 PostLoadMapWithWorld 委托（默认使用当前测试世界）
     * @param WorldOverride 可选覆盖的世界指针（传 nullptr 时使用当前测试世界）
     */
    void BroadcastPostLoadMap(UWorld* WorldOverride = nullptr) const {
        UWorld* TargetWorld = WorldOverride != nullptr ? WorldOverride : GetWorld();
        FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(TargetWorld);
    }

private:
    UGameInstance* GameInstance = nullptr;
    ULoadingSubsystem* Subsystem = nullptr;
};

} // namespace Game::Flow::Test

#endif
