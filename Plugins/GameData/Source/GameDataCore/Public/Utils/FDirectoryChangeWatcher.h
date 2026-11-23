/**
 * @file FDirectoryChangeWatcher.h
 * @brief 通用目录变更监听器（基于目录指纹与 FGameThreadTaskRunner 异步轮询，供配置表与数据热重载使用）
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FCancellationTokenSource.h"
#include "Containers/Ticker.h"
#include "Listener/FValueObserver.h"

#include <atomic>

/**
 * @brief 目录内容轻量指纹（同时覆盖新增、修改、删除与时间戳回滚场景）
 */
struct GAMEDATACORE_API FDirectoryFingerprint {
    /**
     * @brief 匹配目标扩展名的文件总数
     */
    int32 FileCount = 0;

    /**
     * @brief 基于有序路径、文件大小与修改时间戳计算的 64 位 FNV-1a 内容指纹
     */
    uint64 ContentHash = 0;

    /**
     * @brief 判断两份目录指纹是否完全一致
     * @param Other 待比较指纹
     * @return 一致返回 true
     */
    [[nodiscard]] bool operator==(const FDirectoryFingerprint& Other) const noexcept = default;
};

/**
 * @brief 通用目录变更监听器（一个实例对应一个被监听目录，禁止拷贝与移动）
 */
class GAMEDATACORE_API FDirectoryChangeWatcher {
public:
    /**
     * @brief 构造（幂等注册本实例配置键默认值并监听轮询间隔动态变更）
     * @param WatcherName 监听器名称（派生 DirectoryWatch.{Name}.* 配置键）
     * @param EnabledConfigKey 启停开关配置键
     * @param DefaultRelativeDirectory 默认监听目录（相对项目根，绝对路径亦可）
     */
    FDirectoryChangeWatcher(const FName& WatcherName, const FName& EnabledConfigKey, const FString& DefaultRelativeDirectory);

    /**
     * @brief 析构（取消在途任务与配置订阅，移除轮询句柄，杜绝销毁后悬垂访问）
     */
    ~FDirectoryChangeWatcher();
    FDirectoryChangeWatcher(const FDirectoryChangeWatcher&) = delete;
    FDirectoryChangeWatcher& operator=(const FDirectoryChangeWatcher&) = delete;
    FDirectoryChangeWatcher(FDirectoryChangeWatcher&&) = delete;
    FDirectoryChangeWatcher& operator=(FDirectoryChangeWatcher&&) = delete;

    /**
     * @brief 启动轮询（首次启动以当前磁盘目录指纹为基线，重复启动保留既有基线）
     */
    void Start();

    /**
     * @brief 停止轮询并协同取消当前会话的在途后台扫描
     */
    void Stop();

    /**
     * @brief 获取变更通知器
     * @return 通知器引用（载荷为递增的变更序号）
     */
    [[nodiscard]] FValueObserver<uint32>& GetChangeObserver();

    /**
     * @brief 执行目录扫描并计算有序目录指纹（纯静态无副作用，支持工作线程与微基准直测）
     * @param WatchDirectory 监控目录绝对路径
     * @param Extension 目标扩展名（支持 "json"、".json"、"*.json" 或空串匹配全部）
     * @param bRecursive 是否递归扫描子目录
     * @return 确定性目录指纹
     */
    [[nodiscard]] static FDirectoryFingerprint ComputeDirectoryFingerprint(
        const FString& WatchDirectory,
        const FString& Extension,
        bool bRecursive
    );

private:
    /**
     * @brief 单次启停会话的异步扫描上下文（防重叠扫描与取消保护）
     */
    struct FSessionState {
        std::atomic<bool> bIsScanning{false};
        FCancellationTokenSource CancelSource;
    };

    /**
     * @brief 目录扫描配置快照
     */
    struct FScanConfig {
        FString WatchDirectory;
        FString Extension;
        bool bRecursive = true;
    };

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
     * @brief 同步采集当前配置目录的指纹（首次启动建基线使用）
     * @return 目录指纹，目录未配置时返回空指纹
     */
    [[nodiscard]] FDirectoryFingerprint ScanCurrentFingerprint() const;

    /**
     * @brief 解析生效轮询间隔（秒，下限 0.1 秒防空转）
     * @return 轮询间隔秒数
     */
    [[nodiscard]] float ResolveIntervalSeconds() const;

    /**
     * @brief 轮询回调：经 FGameThreadTaskRunner 派发后台扫描
     * @param DeltaTime 帧间隔（未使用）
     * @return 始终返回 true 保持轮询
     */
    bool Tick(float DeltaTime);

    /**
     * @brief 主线程接收后台扫描指纹，发现差异时递增序号并广播观察者
     * @param ScannedFingerprint 后台线程扫描得到的最新目录指纹
     */
    void HandleScanCompleted(const FDirectoryFingerprint& ScannedFingerprint);

    /**
     * @brief 轮询间隔配置动态变更响应回调（始终在 GameThread 安全重建 Ticker）
     * @param NewInterval 新的轮询间隔秒数
     */
    void OnIntervalConfigChanged(float NewInterval);

    FName WatcherName;
    FName EnabledConfigKey;
    FDirectoryFingerprint LastObservedFingerprint;
    bool bHasBaseline = false;
    uint32 ChangeCount = 0;
    FTSTicker::FDelegateHandle TickerHandle;
    TSharedPtr<FObserverSubscription> IntervalSubscription;
    FValueObserver<uint32> ChangeObserver;
    TSharedPtr<FSessionState> SessionState;
    FCancellationTokenSource LifetimeCancelSource;
};
