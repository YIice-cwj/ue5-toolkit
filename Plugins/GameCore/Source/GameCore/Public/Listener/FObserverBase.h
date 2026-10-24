/**
 * @file FObserverBase.h
 * @brief 统一监听基类（非模板）：名称 + 启停开关键 + 版本号 + 广播基础设施
 *
 * 设计思路：承载与值类型无关的通用监听设施；子类（如 FValueObserver<T>）只实现
 * 类型化状态存储与通知分发。构造时按 Listener.{Name}.Enabled 经 FConfigRegistry
 * 注册默认开关并读取当前值，实现"Config 控制所有项目开关"。
 */

#pragma once

#include "CoreMinimal.h"

#include <atomic>

#include "Config/FConfigRegistry.h"

/**
 * @brief 统一监听基类
 */
class GAMECORE_API FObserverBase {
public:
    /**
     * @brief 构造（注册并监听启停开关键）
     * @param InObserverName 监听器名称
     */
    explicit FObserverBase(const FName& InObserverName);

    virtual ~FObserverBase();
    FObserverBase(const FObserverBase&) = delete;
    FObserverBase& operator=(const FObserverBase&) = delete;
    FObserverBase(FObserverBase&&) = delete;
    FObserverBase& operator=(FObserverBase&&) = delete;

    /**
     * @brief 获取监听器名称
     * @return 名称
     */
    [[nodiscard]] FName GetObserverName() const noexcept {
        return ObserverName;
    }

    /**
     * @brief 设置启停开关（同步写穿 FConfigRegistry）
     * @param bInEnabled 目标状态
     */
    void SetEnabled(bool bInEnabled);

    /**
     * @brief 查询当前是否启用（零锁原子读取，由 WatchConfig 与 SetEnabled 双向保持同步）
     * @return 启用返回 true
     */
    [[nodiscard]] bool IsEnabled() const noexcept {
        return EnabledFlag->load(std::memory_order_relaxed);
    }

    /**
     * @brief 获取版本号
     * @return 自 0 起每次广播自增
     */
    [[nodiscard]] uint32 GetVersion() const noexcept {
        return Version.load(std::memory_order_relaxed);
    }

protected:
    /**
     * @brief 统一广播入口：禁用时静默跳过；否则版本自增并触发子类分发
     */
    void Broadcast();

private:
    /** 类型化通知分发（子类实现广播具体载荷） */
    virtual void Notify() = 0;

private:
    /**
     * @brief 按名称生成开关键（Listener.{Name}.Enabled）
     * @param InObserverName 监听器名称
     * @return 开关键
     */
    static FName BuildEnableConfigKey(const FName& InObserverName);

private:
    /** 监听器名称（构成开关键前缀） */
    FName ObserverName;
    /** 启停开关键（Listener.{Name}.Enabled） */
    FName EnableConfigKey;
    /** 配置中心开关监听守卫 */
    TSharedPtr<FObserverSubscription> EnableWatchSubscription;
    /** 当前是否启用标志（共享引用生命周期，防止 Watch 回调快照悬垂） */
    TSharedRef<std::atomic<bool>> EnabledFlag;
    /** 版本号（每次 Broadcast 自增） */
    std::atomic<uint32> Version{0};
};
