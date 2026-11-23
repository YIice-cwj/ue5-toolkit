/**
 * @file FDirectoryChangeWatcher.cpp
 * @brief 通用目录变更监听器实现
 */

#include "Utils/FDirectoryChangeWatcher.h"

#include "Async/FGameThreadTaskRunner.h"
#include "Config/FConfigRegistry.h"
#include "HAL/FileManager.h"
#include "Logging/GameLogCategories.h"
#include "Misc/Paths.h"

namespace {

constexpr const TCHAR* DirectoryWatchKeyPrefix = TEXT("DirectoryWatch.");
constexpr float MinIntervalSeconds = 0.1f;
constexpr uint64 Fnv1aOffsetBasis = 14695981039346656037ULL;
constexpr uint64 Fnv1aPrime = 1099511628211ULL;

/**
 * @brief 将任意字节区间并入 64 位 FNV-1a 哈希值
 * @param Hash 当前累加哈希状态
 * @param Data 字节数据起始指针
 * @param ByteCount 字节数量
 */
void HashBytesFnv1a(uint64& Hash, const uint8* Data, int32 ByteCount) {
    for (int32 Index = 0; Index < ByteCount; ++Index) {
        Hash ^= static_cast<uint64>(Data[Index]);
        Hash *= Fnv1aPrime;
    }
}

/**
 * @brief 归一化扩展名配置文本（去除前导 '*' 与 '.'，空串或 "*" 表示匹配全部文件）
 * @param RawExtension 原始扩展名配置字符串
 * @return 纯扩展名（如 "json"）或空串（表示全部文件）
 */
FString NormalizeExtension(const FString& RawExtension) {
    FString Cleaned = RawExtension.TrimStartAndEnd();
    while (Cleaned.StartsWith(TEXT("*")) || Cleaned.StartsWith(TEXT("."))) {
        Cleaned.RightChopInline(1, EAllowShrinking::No);
    }
    return Cleaned;
}

}  // namespace

/**
 * @brief 构造（幂等注册本实例配置键默认值并监听轮询间隔动态变更）
 * @param WatcherName 监听器名称（派生 DirectoryWatch.{Name}.* 配置键）
 * @param EnabledConfigKey 启停开关配置键
 * @param DefaultRelativeDirectory 默认监听目录（相对项目根，绝对路径亦可）
 */
FDirectoryChangeWatcher::FDirectoryChangeWatcher(
    const FName& WatcherName,
    const FName& EnabledConfigKey,
    const FString& DefaultRelativeDirectory
)
    : WatcherName(WatcherName)
    , EnabledConfigKey(EnabledConfigKey)
    , ChangeObserver(FName(*(FString(DirectoryWatchKeyPrefix) + WatcherName.ToString()))) {
    FConfigRegistry& Registry = FConfigRegistry::Get();
    Registry.RegisterConfig<bool>(EnabledConfigKey, true);
    Registry.RegisterConfig<FString>(MakeConfigKey(TEXT("Directory")), DefaultRelativeDirectory);
    Registry.RegisterConfig<FString>(MakeConfigKey(TEXT("Extension")), TEXT("json"));
    Registry.RegisterConfig<bool>(MakeConfigKey(TEXT("Recursive")), true);
    Registry.RegisterConfig<float>(MakeConfigKey(TEXT("IntervalSeconds")), 2.0f);

    const FCancellationToken LifetimeToken = LifetimeCancelSource.GetToken();
    IntervalSubscription = Registry.WatchConfig<float>(
        MakeConfigKey(TEXT("IntervalSeconds")),
        [this, LifetimeToken](float NewInterval) {
            if (LifetimeToken.IsCancellationRequested()) {
                return;
            }
            if (IsInGameThread()) {
                OnIntervalConfigChanged(NewInterval);
                return;
            }
            FGameThreadTaskRunner::RunOnGameThread([this, LifetimeToken, NewInterval]() {
                if (!LifetimeToken.IsCancellationRequested()) {
                    OnIntervalConfigChanged(NewInterval);
                }
            });
        }
    );
}

/**
 * @brief 析构（取消在途任务与配置订阅，移除轮询句柄，杜绝销毁后悬垂访问）
 */
FDirectoryChangeWatcher::~FDirectoryChangeWatcher() {
    LifetimeCancelSource.Cancel();
    if (IntervalSubscription.IsValid()) {
        IntervalSubscription->Reset();
    }
    Stop();
}

/**
 * @brief 启动轮询（首次启动以当前磁盘目录指纹为基线，重复启动保留既有基线）
 */
void FDirectoryChangeWatcher::Start() {
    Stop();
    SessionState = MakeShared<FSessionState>();
    if (!bHasBaseline) {
        LastObservedFingerprint = ScanCurrentFingerprint();
        bHasBaseline = true;
    }

    const float Interval = ResolveIntervalSeconds();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FDirectoryChangeWatcher::Tick),
        Interval
    );

    const TOptional<FScanConfig> ScanConfig = ResolveScanConfig();
    GAME_LOG(Log, TEXT("【目录监听】启动目录监听 [%s]: 目录=[%s], 扩展名=[%s], 递归=%s, 轮询间隔=%.2f 秒"),
        *WatcherName.ToString(),
        ScanConfig.IsSet() ? *ScanConfig->WatchDirectory : TEXT("未配置"),
        ScanConfig.IsSet() ? *ScanConfig->Extension : TEXT(""),
        (ScanConfig.IsSet() && ScanConfig->bRecursive) ? TEXT("True") : TEXT("False"),
        Interval);
}

/**
 * @brief 停止轮询并协同取消当前会话的在途后台扫描
 */
void FDirectoryChangeWatcher::Stop() {
    if (SessionState.IsValid()) {
        SessionState->CancelSource.Cancel();
    }
    if (TickerHandle.IsValid()) {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
        GAME_LOG(Log, TEXT("【目录监听】停止目录监听 [%s]。"), *WatcherName.ToString());
    }
}

/**
 * @brief 获取变更通知器
 * @return 通知器引用（载荷为递增的变更序号）
 */
FValueObserver<uint32>& FDirectoryChangeWatcher::GetChangeObserver() {
    return ChangeObserver;
}

/**
 * @brief 执行目录扫描并计算有序目录指纹（纯静态无副作用，支持工作线程与微基准直测）
 * @param WatchDirectory 监控目录绝对路径
 * @param Extension 目标扩展名（支持 "json"、".json"、"*.json" 或空串匹配全部）
 * @param bRecursive 是否递归扫描子目录
 * @return 确定性目录指纹
 */
FDirectoryFingerprint FDirectoryChangeWatcher::ComputeDirectoryFingerprint(
    const FString& WatchDirectory,
    const FString& Extension,
    bool bRecursive
) {
    FDirectoryFingerprint Fingerprint;
    if (WatchDirectory.IsEmpty() || !IFileManager::Get().DirectoryExists(*WatchDirectory)) {
        return Fingerprint;
    }

    const FString CleanExt = NormalizeExtension(Extension);
    TArray<FString> FoundFiles;
    if (bRecursive) {
        const FString Wildcard = CleanExt.IsEmpty() ? TEXT("*") : (FString(TEXT("*.")) + CleanExt);
        IFileManager::Get().FindFilesRecursive(FoundFiles, *WatchDirectory, *Wildcard, true, false);
    } else {
        TArray<FString> FoundFileNames;
        const TCHAR* ExtensionFilter = CleanExt.IsEmpty() ? nullptr : *CleanExt;
        IFileManager::Get().FindFiles(FoundFileNames, *WatchDirectory, ExtensionFilter);
        FoundFiles.Reserve(FoundFileNames.Num());
        for (const FString& FileName : FoundFileNames) {
            FoundFiles.Add(WatchDirectory / FileName);
        }
    }

    FoundFiles.Sort();
    Fingerprint.FileCount = FoundFiles.Num();
    if (FoundFiles.IsEmpty()) {
        return Fingerprint;
    }

    uint64 Hash = Fnv1aOffsetBasis;
    IFileManager& FileManager = IFileManager::Get();
    for (const FString& FilePath : FoundFiles) {
        const FTCHARToUTF8 PathUtf8(*FilePath);
        const uint32 PathLen = static_cast<uint32>(PathUtf8.Length());
        const int64 FileSize = FileManager.FileSize(*FilePath);
        const int64 TimestampTicks = FileManager.GetTimeStamp(*FilePath).GetTicks();
        HashBytesFnv1a(Hash, reinterpret_cast<const uint8*>(&PathLen), static_cast<int32>(sizeof(PathLen)));
        HashBytesFnv1a(Hash, reinterpret_cast<const uint8*>(PathUtf8.Get()), static_cast<int32>(PathLen));
        HashBytesFnv1a(Hash, reinterpret_cast<const uint8*>(&FileSize), static_cast<int32>(sizeof(FileSize)));
        HashBytesFnv1a(Hash, reinterpret_cast<const uint8*>(&TimestampTicks), static_cast<int32>(sizeof(TimestampTicks)));
    }
    Fingerprint.ContentHash = Hash;
    return Fingerprint;
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
 * @brief 同步采集当前配置目录的指纹（首次启动建基线使用）
 * @return 目录指纹，目录未配置时返回空指纹
 */
FDirectoryFingerprint FDirectoryChangeWatcher::ScanCurrentFingerprint() const {
    const TOptional<FScanConfig> ScanConfig = ResolveScanConfig();
    return ScanConfig.IsSet()
        ? ComputeDirectoryFingerprint(ScanConfig->WatchDirectory, ScanConfig->Extension, ScanConfig->bRecursive)
        : FDirectoryFingerprint();
}

/**
 * @brief 解析生效轮询间隔（秒，下限 0.1 秒防空转）
 * @return 轮询间隔秒数
 */
float FDirectoryChangeWatcher::ResolveIntervalSeconds() const {
    return FMath::Max(
        FConfigRegistry::Get().GetConfig<float>(MakeConfigKey(TEXT("IntervalSeconds")), 2.0f),
        MinIntervalSeconds
    );
}

/**
 * @brief 轮询回调：经 FGameThreadTaskRunner 派发后台扫描
 * @param DeltaTime 帧间隔（未使用）
 * @return 始终返回 true 保持轮询
 */
bool FDirectoryChangeWatcher::Tick([[maybe_unused]] float DeltaTime) {
    if (!FConfigRegistry::Get().GetConfig<bool>(EnabledConfigKey, true)) {
        return true;
    }
    if (!SessionState.IsValid() || SessionState->bIsScanning.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }

    const TOptional<FScanConfig> ScanConfig = ResolveScanConfig();
    if (!ScanConfig.IsSet()) {
        SessionState->bIsScanning.store(false, std::memory_order_release);
        return true;
    }

    const TSharedPtr<FSessionState> CapturedSession = SessionState;
    const FCancellationToken SessionToken = CapturedSession->CancelSource.GetToken();
    const FCancellationToken LifetimeToken = LifetimeCancelSource.GetToken();
    const FScanConfig CapturedConfig = *ScanConfig;
    TSharedPtr<void, ESPMode::ThreadSafe> ScanFlagGuard = MakeShareable<void>(
        nullptr,
        [CapturedSession](void*) {
            CapturedSession->bIsScanning.store(false, std::memory_order_release);
        }
    );

    FGameThreadTaskRunner::Run(SessionToken, [this, CapturedSession, SessionToken, LifetimeToken, CapturedConfig, ScanFlagGuard = MoveTemp(ScanFlagGuard)]() mutable {
        if (SessionToken.IsCancellationRequested() || LifetimeToken.IsCancellationRequested()) {
            ScanFlagGuard.Reset();
            return;
        }

        const FDirectoryFingerprint ScannedFingerprint = ComputeDirectoryFingerprint(
            CapturedConfig.WatchDirectory,
            CapturedConfig.Extension,
            CapturedConfig.bRecursive
        );

        FGameThreadTaskRunner::RunOnGameThread([this, SessionToken, LifetimeToken, ScannedFingerprint, ScanFlagGuard = MoveTemp(ScanFlagGuard)]() mutable {
            ScanFlagGuard.Reset();
            if (SessionToken.IsCancellationRequested() || LifetimeToken.IsCancellationRequested()) {
                return;
            }
            HandleScanCompleted(ScannedFingerprint);
        });
    });

    return true;
}

/**
 * @brief 主线程接收后台扫描指纹，发现差异时递增序号并广播观察者
 * @param ScannedFingerprint 后台线程扫描得到的最新目录指纹
 */
void FDirectoryChangeWatcher::HandleScanCompleted(const FDirectoryFingerprint& ScannedFingerprint) {
    if (ScannedFingerprint == LastObservedFingerprint) {
        return;
    }
    LastObservedFingerprint = ScannedFingerprint;
    ++ChangeCount;
    GAME_LOG(Log, TEXT("【目录监听】检测到目录变更 [%s]: 文件数=%d, 变更计数值=%u"),
        *WatcherName.ToString(), ScannedFingerprint.FileCount, ChangeCount);
    ChangeObserver.Set(ChangeCount);
}

/**
 * @brief 轮询间隔配置动态变更响应回调（始终在 GameThread 安全重建 Ticker）
 * @param NewInterval 新的轮询间隔秒数
 */
void FDirectoryChangeWatcher::OnIntervalConfigChanged(float NewInterval) {
    if (!TickerHandle.IsValid()) {
        return;
    }
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FDirectoryChangeWatcher::Tick),
        FMath::Max(NewInterval, MinIntervalSeconds)
    );
}
