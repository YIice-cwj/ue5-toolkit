/**
 * @file FObserverBase.h
 * @brief 统一监听基类（非模板）：名称 + 启停开关键 + 版本号 + 广播基础设施
 *
 * 设计思路：承载与值类型无关的通用监听设施；子类（如 FValueObserver<T>）只实现
 * 类型化状态存储与通知分发。构造时按 Listener.{Name}.Enabled 经 FConfigRegistry
 * 注册默认开关，所有启用状态读取均以 Config 为唯一真源。
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
     * @brief 构造监听基类并注册启停开关配置键，Config 为启用状态唯一真源
     * @param InObserverName 监听器唯一名称
     */
    explicit FObserverBase(const FName& InObserverName);

    /**
     * @brief 虚析构
     */
    virtual ~FObserverBase();
    FObserverBase(const FObserverBase&) = delete;
    FObserverBase& operator=(const FObserverBase&) = delete;
    FObserverBase(FObserverBase&&) = delete;
    FObserverBase& operator=(FObserverBase&&) = delete;

    /**
     * @brief 获取监听器名称
     * @return 监听器名称
     */
    [[nodiscard]] FName GetObserverName() const noexcept {
        return ObserverName;
    }

    /**
     * @brief 设置启停开关（同步写穿至 FConfigRegistry 对应键）
     * @param bInEnabled 目标启用状态
     */
    void SetEnabled(bool bInEnabled);

    /**
     * @brief 查询 Config 中的当前启用状态
     * @return 启用返回 true，禁用返回 false
     */
    [[nodiscard]] bool IsEnabled() const noexcept {
        return FConfigRegistry::Get().GetConfig<bool>(EnableConfigKey, true);
    }

    /**
     * @brief 获取观察者当前版本号
     * @return 自 0 开始、每次启用状态下的值提交递增的版本号
     */
    [[nodiscard]] uint32 GetVersion() const noexcept {
        return Version.load(std::memory_order_relaxed);
    }

protected:
    /**
     * @brief 广播前读取唯一启用真源，启用时递增版本号
     * @return 本次值提交需要通知订阅者返回 true
     */
    bool TryPrepareBroadcast();

private:
    /**
     * @brief 根据名称生成标准开关键（Listener.{Name}.Enabled）
     * @param InObserverName 监听器名称
     * @return 格式化后的完整配置键
     */
    static FName BuildEnableConfigKey(const FName& InObserverName);

private:
    /**
     * @brief 监听器名称（构成开关键前缀）
     */
    FName ObserverName;

    /**
     * @brief 启停开关键（Listener.{Name}.Enabled）
     */
    FName EnableConfigKey;

    /**
     * @brief 版本号（每次启用状态下的值提交自增）
     */
    std::atomic<uint32> Version{0};
};
