/**
 * @file FDirectoryChangeWatcher.h
 * @brief 通用目录变更监听器（轮询改动时间戳，Core 级通用设施）
 *
 * 设计思路：按实例名派生配置键（监听目录/扩展名/递归/间隔），FTSTicker 周期扫描目标目录内匹配文件的
 * 最近修改时间戳；时间戳前进即自增并广播递增的变更序号，消费方订阅该通知决定后续动作。
 * 首次启动以当前磁盘时间戳为基线，只响应此后发生的变更；开关关闭期间冻结基线
 * （重新开启后一次性同步此间变更）；已启动实例重复 Start 保留既有基线，不吞掉间隙内的变更。
 * 消费方（如静态数据热更观察者）退化为薄订阅者，不再各自实现轮询。
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Misc/DateTime.h"

#include <atomic>

#include "Listener/FValueObserver.h"

/**
 * @brief 通用目录变更监听器（一个实例对应一个被监听目录，禁止拷贝与移动）
 */
class GAMECORE_API FDirectoryChangeWatcher {
private:
    /** 异步扫描上下文（防 UAF 与重叠并发） */
    struct FWatcherContext {
        std::atomic<bool> bIsScanning{false};
        std::atomic<bool> bIsStopped{false};
    };

    /** 目录扫描配置快照 */
    struct FScanConfig {
        FString WatchDirectory;
        FString Extension;
        bool bRecursive = true;
    };

public:
    /**
     * @brief 构造（幂等注册本实例配置键默认值）
     * @param WatcherName 监听器名称（派生 DirectoryWatch.{Name}.* 配置键）
     * @param EnabledConfigKey 启停开关配置键
     * @param DefaultRelativeDirectory 默认监听目录（相对项目根，绝对路径亦可）
     */
    FDirectoryChangeWatcher(const FName& WatcherName, const FName& EnabledConfigKey, const FString& DefaultRelativeDirectory);

    /**
     * @brief 析构（移除轮询句柄，杜绝销毁后 ticker 回调悬垂 this）
     */
    ~FDirectoryChangeWatcher();
    FDirectoryChangeWatcher(const FDirectoryChangeWatcher&) = delete;
    FDirectoryChangeWatcher& operator=(const FDirectoryChangeWatcher&) = delete;
    FDirectoryChangeWatcher(FDirectoryChangeWatcher&&) = delete;
    FDirectoryChangeWatcher& operator=(FDirectoryChangeWatcher&&) = delete;

    /**
     * @brief 启动轮询（首次启动以当前磁盘时间戳为基线，重复启动保留既有基线）
     */
    void Start();

    /**
     * @brief 停止轮询
     */
    void Stop();

    /**
     * @brief 获取变更通知器
     * @return 通知器引用（载荷为递增的变更序号）
     */
    [[nodiscard]] FValueObserver<uint32>& GetChangeObserver();

private:
    /**
     * @brief 拼接本实例的配置键
     * @param Suffix 键后缀
     * @return 完整配置键
     */
    [[nodiscard]] FName MakeConfigKey(const TCHAR* Suffix) const;

    /**
     * @brief 解析当前配置的监听目录、扩展名与递归标志
     * @return 目录配置非空时返回有效扫描配置，否则返回空
     */
    [[nodiscard]] TOptional<FScanConfig> ResolveScanConfig() const;

    /**
     * @brief 采集目标目录内匹配文件的最近修改时间戳
     * @return 最大修改时间戳，目录未配置或为空返回最小值
     */
    [[nodiscard]] FDateTime ScanLatestTimestamp() const;

    /**
     * @brief 解析生效轮询间隔（秒，下限 0.1 秒防空转）
     * @return 轮询间隔
     */
    [[nodiscard]] float ResolveIntervalSeconds() const;

    /**
     * @brief 轮询回调：派发异步任务扫描并在时间戳前进时回主线程广播
     * @param DeltaTime 帧间隔（未使用）
     * @return 始终 true 保持轮询
     */
    bool Tick(float DeltaTime);

private:
    /**
     * @brief 执行目录扫描（工作线程执行，纯函数零副作用）
     * @param WatchDirectory 监控目录绝对路径
     * @param Extension 目标扩展名
     * @param bRecursive 是否递归
     * @return 最大修改时间戳
     */
    [[nodiscard]] static FDateTime PerformScan(const FString& WatchDirectory, const FString& Extension, bool bRecursive);

    void OnIntervalConfigChanged(float NewInterval);

private:
    /** 监听器名称（派生配置键与通知器名称） */
    FName WatcherName;

    /** 启停开关配置键（语义由消费方定义：关闭即冻结观察基线） */
    FName EnabledConfigKey;

    /** 最近一次观察到的目录内文件最大修改时间戳 */
    FDateTime LastObservedTimestamp = FDateTime::MinValue();

    /** 已发生的变更序号（递增，作为通知载荷） */
    uint32 ChangeCount = 0;

    /** 轮询句柄 */
    FTSTicker::FDelegateHandle TickerHandle;

    /** 轮询间隔配置订阅守卫 */
    TSharedPtr<FObserverSubscription> IntervalSubscription;

    /** 变更通知器（载荷为递增的变更序号） */
    FValueObserver<uint32> ChangeObserver;

    /** 异步上下文 */
    TSharedPtr<FWatcherContext> Context;

    /** 存活令牌（防止 Watch 回调快照析构竞态） */
    TSharedRef<std::atomic<bool>> AliveToken;
};
