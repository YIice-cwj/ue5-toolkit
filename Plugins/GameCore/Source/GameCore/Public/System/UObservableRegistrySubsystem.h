/**
 * @file UObservableRegistrySubsystem.h
 * @brief World 作用域的可观察值注册表子系统
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Listener/FObservableRegistry.h"

#include "UObservableRegistrySubsystem.generated.h"

/**
 * @brief World 作用域的任意数据观察者注册表（随世界创建/销毁自动隔离）
 *
 * 业务方按 Key 注册任意类型数据（暴击/闪避/冷却等），订阅方只依赖 Key 与类型，
 * 无需感知写入方；注册表随世界生命周期自动回收，不跨关卡泄漏。
 */
UCLASS()
class GAMECORE_API UObservableRegistrySubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
    /**
     * @brief 静态工具方法：通过任意上下文对象获取当前世界的注册表子系统
     * @param WorldContextObject 世界上下文对象
     * @return 子系统指针，不存在返回 nullptr
     */
    static UObservableRegistrySubsystem* Get(const UObject* WorldContextObject);

    /**
     * @brief 世界销毁生命周期回调：清空观察者注册表与所有已注册观察者
     */
    void Deinitialize() override;

    /**
     * @brief 获取当前世界的观察者注册表
     * @return 注册表引用
     */
    [[nodiscard]] Game::Core::FObservableRegistry& GetRegistry();

    /**
     * @brief 获取当前世界的观察者注册表（只读）
     * @return 注册表引用
     */
    [[nodiscard]] const Game::Core::FObservableRegistry& GetRegistry() const;

private:
    Game::Core::FObservableRegistry Registry;
};
