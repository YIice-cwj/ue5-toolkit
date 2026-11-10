/**
 * @file FObserverBase.h
 * @brief 统一监听基类（非模板）：名称 + 启停开关键 + 版本号 + 广播基础设施
 *
 * 设计思路：承载与值类型无关的通用监听设施；子类（如 FValueObserver<T>）只实现
 * 类型化状态存储与通知分发。构造时按 Listener.{Name}.Enabled 经 FConfigRegistry
 * 注册默认开关，所有启用状态读取均以 Config 为唯一真源。
 *
 * 生命周期契约：ObserverName 应对应具名业务观察域（而非高频临时生成的随机 UUID），
 * 对应的配置键 Listener.{Name}.Enabled 在 FConfigRegistry 中跨观察者实例生命周期保留，
 * 以便热重载或重新实例化同名观察者时延续外部注入的启停覆盖状态；而 EnableWatchSubscription
 * 随 FObserverBase 析构自动注销，零回调泄漏。
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
     * @brief 查询当前启用状态（由本地原子镜像提供零锁热路径读取，通过 WatchConfig 与 FConfigRegistry 实时同步）
     * @return 启用返回 true，禁用返回 false
     */
    [[nodiscard]] bool IsEnabled() const noexcept {
        return bCachedEnabled.load(std::memory_order_relaxed);
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
     * @brief 配置中心启停开关响应式订阅守卫
     */
    TSharedPtr<FObserverSubscription> EnableWatchSubscription;

    /**
     * @brief 本地原子启停镜像（消除每次 Set/广播时的全局读写锁与哈希查询开销）
     */
    std::atomic<bool> bCachedEnabled{true};

    /**
     * @brief 版本号（每次启用状态下的值提交自增）
     */
    std::atomic<uint32> Version{0};
};
