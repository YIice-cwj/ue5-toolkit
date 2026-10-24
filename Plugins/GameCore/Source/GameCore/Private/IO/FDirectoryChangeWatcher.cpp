/**
 * @file FDirectoryChangeWatcher.cpp
 * @brief 通用目录变更监听器实现
 */

#include "IO/FDirectoryChangeWatcher.h"

#include "Async/Async.h"
#include "Config/FConfigRegistry.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace {

/** 配置键前缀 */
constexpr const TCHAR* DirectoryWatchKeyPrefix = TEXT("DirectoryWatch.");

/** 轮询间隔下限（秒，防空转） */
constexpr float MinIntervalSeconds = 0.1f;

}  // namespace

/**
 * @brief 构造（幂等注册本实例配置键默认值）
 * @param WatcherName 监听器名称（派生 DirectoryWatch.{Name}.* 配置键）
 * @param EnabledConfigKey 启停开关配置键
 * @param DefaultRelativeDirectory 默认监听目录（相对项目根，绝对路径亦可）
 */
FDirectoryChangeWatcher::FDirectoryChangeWatcher(const FName& WatcherName, const FName& EnabledConfigKey, const FString& DefaultRelativeDirectory)
    : WatcherName(WatcherName)
    , EnabledConfigKey(EnabledConfigKey)
    , ChangeObserver(FName(*(FString(DirectoryWatchKeyPrefix) + WatcherName.ToString())))
    , AliveToken(MakeShared<std::atomic<bool>>(true)) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    Registry.RegisterConfig<bool>(EnabledConfigKey, true);
    Registry.RegisterConfig<FString>(MakeConfigKey(TEXT("Directory")), DefaultRelativeDirectory);
    Registry.RegisterConfig<FString>(MakeConfigKey(TEXT("Extension")), TEXT("json"));
    Registry.RegisterConfig<bool>(MakeConfigKey(TEXT("Recursive")), true);
    Registry.RegisterConfig<float>(MakeConfigKey(TEXT("IntervalSeconds")), 2.0f);
    TWeakPtr<std::atomic<bool>> WeakToken = AliveToken;
    IntervalSubscription = Registry.WatchConfig<float>(MakeConfigKey(TEXT("IntervalSeconds")), [WeakToken, this](float NewInterval) {
        if (TSharedPtr<std::atomic<bool>> Pinned = WeakToken.Pin()) {
            if (Pinned->load(std::memory_order_acquire)) {
                OnIntervalConfigChanged(NewInterval);
            }
        }
    });
}

/**
 * @brief 析构（移除轮询句柄，杜绝销毁后 ticker 回调悬垂 this）
 */
FDirectoryChangeWatcher::~FDirectoryChangeWatcher() {
    AliveToken->store(false, std::memory_order_release);
    if (IntervalSubscription.IsValid()) {
        IntervalSubscription->Reset();
    }
    Stop();
}

/**
 * @brief 启动轮询（首次启动以当前磁盘时间戳为基线，重复启动保留既有基线）
 */
void FDirectoryChangeWatcher::Start() {
    const bool bTickerActive = TickerHandle.IsValid();
    Stop();
    Context = MakeShared<FWatcherContext>();
    if (!bTickerActive) {
        LastObservedTimestamp = ScanLatestTimestamp();
    }
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FDirectoryChangeWatcher::Tick), ResolveIntervalSeconds());
}

/**
 * @brief 停止轮询
 */
void FDirectoryChangeWatcher::Stop() {
    if (Context.IsValid()) {
        Context->bIsStopped.store(true, std::memory_order_release);
    }
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    TickerHandle.Reset();
}

/**
 * @brief 获取变更通知器
 * @return 通知器引用（载荷为递增的变更序号）
 */
FValueObserver<uint32>& FDirectoryChangeWatcher::GetChangeObserver() {
    return ChangeObserver;
}

/**
 * @brief 拼接本实例的配置键
 * @param Suffix 键后缀
 * @return 完整配置键
 */
FName FDirectoryChangeWatcher::MakeConfigKey(const TCHAR* Suffix) const {
    return FName(*(FString(DirectoryWatchKeyPrefix) + WatcherName.ToString() + TEXT(".") + Suffix));
}

/**
 * @brief 解析当前配置的监听目录、扩展名与递归标志
 * @return 目录配置非空时返回有效扫描配置，否则返回空
 */
TOptional<FDirectoryChangeWatcher::FScanConfig> FDirectoryChangeWatcher::ResolveScanConfig() const {
    const FConfigRegistry& Registry = FConfigRegistry::Get();
    const FString ConfiguredDirectory = Registry.GetConfig<FString>(MakeConfigKey(TEXT("Directory")), TEXT(""));
    if (ConfiguredDirectory.IsEmpty()) {
        return NullOpt;
    }
    FScanConfig Config;
    Config.Extension = Registry.GetConfig<FString>(MakeConfigKey(TEXT("Extension")), TEXT("json"));
    Config.bRecursive = Registry.GetConfig<bool>(MakeConfigKey(TEXT("Recursive")), true);
    Config.WatchDirectory = FPaths::IsRelative(ConfiguredDirectory)
        ? (FPaths::ProjectDir() / ConfiguredDirectory)
        : ConfiguredDirectory;
    return Config;
}

/**
 * @brief 采集目标目录内匹配文件的最近修改时间戳
 * @return 最大修改时间戳，目录未配置或为空返回最小值
 */
FDateTime FDirectoryChangeWatcher::ScanLatestTimestamp() const {
    const TOptional<FScanConfig> ScanConfig = ResolveScanConfig();
    return ScanConfig.IsSet()
        ? PerformScan(ScanConfig->WatchDirectory, ScanConfig->Extension, ScanConfig->bRecursive)
        : FDateTime::MinValue();
}

/**
 * @brief 解析生效轮询间隔（秒，下限 0.1 秒防空转）
 * @return 轮询间隔
 */
float FDirectoryChangeWatcher::ResolveIntervalSeconds() const {
    return FMath::Max(FConfigRegistry::Get().GetConfig<float>(MakeConfigKey(TEXT("IntervalSeconds")), 2.0f), MinIntervalSeconds);
}

void FDirectoryChangeWatcher::OnIntervalConfigChanged(float NewInterval) {
    if (TickerHandle.IsValid()) {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateRaw(this, &FDirectoryChangeWatcher::Tick), FMath::Max(NewInterval, MinIntervalSeconds));
    }
}

/**
 * @brief 轮询回调：派发异步任务扫描并在时间戳前进时回主线程广播
 * @param DeltaTime 帧间隔（未使用）
 * @return 始终 true 保持轮询
 */
bool FDirectoryChangeWatcher::Tick([[maybe_unused]] float DeltaTime) {
    if (!FConfigRegistry::Get().GetConfig<bool>(EnabledConfigKey, true)) {
        return true;
    }
    if (Context->bIsScanning.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }

    const TOptional<FScanConfig> ScanConfig = ResolveScanConfig();
    if (!ScanConfig.IsSet()) {
        Context->bIsScanning.store(false, std::memory_order_release);
        return true;
    }

    const FString WatchDirectory = ScanConfig->WatchDirectory;
    const FString Extension = ScanConfig->Extension;
    const bool bRecursive = ScanConfig->bRecursive;

    Async(EAsyncExecution::ThreadPool, [CapturedContext = Context, this, WatchDirectory, Extension, bRecursive]() {
        if (CapturedContext->bIsStopped.load(std::memory_order_acquire)) {
            CapturedContext->bIsScanning.store(false, std::memory_order_release);
            return;
        }

        const FDateTime ScannedTimestamp = PerformScan(WatchDirectory, Extension, bRecursive);

        AsyncTask(ENamedThreads::GameThread, [CapturedContext, this, ScannedTimestamp]() {
            CapturedContext->bIsScanning.store(false, std::memory_order_release);
            if (CapturedContext->bIsStopped.load(std::memory_order_acquire)) {
                return;
            }
            if (ScannedTimestamp > LastObservedTimestamp) {
                LastObservedTimestamp = ScannedTimestamp;
                ++ChangeCount;
                ChangeObserver.Set(ChangeCount);
            }
        });
    });

    return true;
}

/**
 * @brief 执行目录扫描（工作线程执行，纯函数零副作用）
 * @param WatchDirectory 监控目录绝对路径
 * @param Extension 目标扩展名
 * @param bRecursive 是否递归
 * @return 最大修改时间戳
 */
FDateTime FDirectoryChangeWatcher::PerformScan(const FString& WatchDirectory, const FString& Extension, bool bRecursive) {
    TArray<FString> FoundFiles;
    if (bRecursive) {
        IFileManager::Get().FindFilesRecursive(FoundFiles, *WatchDirectory, *(FString(TEXT("*.")) + Extension), true, false);
    } else {
        TArray<FString> FoundFileNames;
        IFileManager::Get().FindFiles(FoundFileNames, *WatchDirectory, *(FString(TEXT("*.")) + Extension));
        FoundFiles.Reserve(FoundFileNames.Num());
        for (const FString& FileName : FoundFileNames) {
            FoundFiles.Add(WatchDirectory / FileName);
        }
    }

    FDateTime LatestTimestamp = FDateTime::MinValue();
    for (const FString& FilePath : FoundFiles) {
        const FDateTime FileTimestamp = IFileManager::Get().GetTimeStamp(*FilePath);
        if (FileTimestamp > LatestTimestamp) {
            LatestTimestamp = FileTimestamp;
        }
    }
    return LatestTimestamp;
}
