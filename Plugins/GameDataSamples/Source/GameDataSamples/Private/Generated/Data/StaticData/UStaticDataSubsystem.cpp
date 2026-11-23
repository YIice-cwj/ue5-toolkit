/**
 * @file UStaticDataSubsystem.cpp
 * @brief 全局静态数据子系统实现
 */
#include "Generated/Data/StaticData/UStaticDataSubsystem.h"

#include "Async/FGameThreadTaskRunner.h"
#include "Config/GameCompileSwitches.h"
#include "Containers/Ticker.h"
#include "Core/DataErrorCodes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "StaticData/FStaticDataRepository.h"
#include "Types/TCancelableScopeGuard.h"
#include "Logging/GameLogCategories.h"
#include "Generated/Data/StaticData/StaticDataSerialization.h"

#include "Generated/Data/StaticData/Item/ItemStaticDataInterface.h"
#include "Generated/Data/StaticData/Shop/ShopStaticDataInterface.h"

namespace {

/**
 * @brief 生成式二进制档的运行时路径（内容目录相对路径，随生成配置固化）
 * @return .bin 绝对路径（编辑器直读磁盘，发布包内经 UFS 从 Pak 读取）
 */
FString GetStaticDataBinPath() {
    return FPaths::ProjectContentDir() / TEXT("DataTables_Bin/StaticDataCache.bin");
}

/**
 * @brief 解析冷启动装载通道的强制开关（命令行 -GameStaticDataChannel=json 强制 JSON 通道）
 * @return 是否优先走 .bin 通道（未指定开关即默认优先）
 */
bool ResolvePreferBinChannel() {
    FString ChannelName;
    if (FParse::Value(FCommandLine::Get(), TEXT("GameStaticDataChannel="), ChannelName)) {
        return !ChannelName.Equals(TEXT("json"), ESearchCase::IgnoreCase);
    }
    return true;
}

/**
 * @brief 自 JSON 通道整体装载各域暂存数据（开发期热更与 .bin 回落通道）
 * @param OutItemData Item 域暂存容器
 * @param OutShopData Shop 域暂存容器
 * @return 全部域装载成功返回 Success，否则返回首个失败结果
 */
TGameStatusResult<void> LoadStagingFromJson(FItemStaticData& OutItemData, FShopStaticData& OutShopData) {
    const double LoadStartSeconds = FPlatformTime::Seconds();
    if (const TGameStatusResult<void> Res_Item = OutItemData.LoadAllFromJson(); Res_Item.IsFailure()) {
        return Res_Item;
    }
    if (const TGameStatusResult<void> Res_Shop = OutShopData.LoadAllFromJson(); Res_Shop.IsFailure()) {
        return Res_Shop;
    }

    GAME_LOG(Log, TEXT("【静态数据子系统】本批次数据来源：JSON 通道。"));
    GAME_LOG(Log, TEXT("【静态数据装载耗时】JSON 通道反序列化 %.3f ms。"), (FPlatformTime::Seconds() - LoadStartSeconds) * 1000.0);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 尝试自 .bin 通道整体装载各域暂存数据（任一步失败即返回精确错误，交由调用方回落 JSON）
 * @param InBinPath .bin 文件路径
 * @param OutItemData Item 域暂存容器
 * @param OutShopData Shop 域暂存容器
 * @return 全部表提取成功返回 Success，否则返回失败结果
 */
TGameStatusResult<void> TryLoadStagingFromBin(const FString& InBinPath, FItemStaticData& OutItemData, FShopStaticData& OutShopData) {
    const double LoadStartSeconds = FPlatformTime::Seconds();
    FStaticDataRepository Repository;
    RegisterGameStaticDataRowSerializers(Repository);

    const TGameStatusResult<int32> ImportResult = Repository.ImportFromFile(InBinPath);
    if (ImportResult.IsFailure()) {
        GAME_LOG(Warning, TEXT("【静态数据子系统】.bin 通道装载失败（错误码 %d: %s），已回落 JSON 通道。"), ImportResult.GetError().ErrorCode, *ImportResult.GetError().Message);
        return TGameStatusResult<void>::Failure(ImportResult.GetError());
    }

    if (const TGameStatusResult<void> ExtractRes_Item = OutItemData.LoadAllFromRepository(Repository); ExtractRes_Item.IsFailure()) {
        GAME_LOG(Warning, TEXT("【静态数据子系统】.bin 通道表提取失败（错误码 %d: %s），已回落 JSON 通道。"), ExtractRes_Item.GetError().ErrorCode, *ExtractRes_Item.GetError().Message);
        return ExtractRes_Item;
    }
    if (const TGameStatusResult<void> ExtractRes_Shop = OutShopData.LoadAllFromRepository(Repository); ExtractRes_Shop.IsFailure()) {
        GAME_LOG(Warning, TEXT("【静态数据子系统】.bin 通道表提取失败（错误码 %d: %s），已回落 JSON 通道。"), ExtractRes_Shop.GetError().ErrorCode, *ExtractRes_Shop.GetError().Message);
        return ExtractRes_Shop;
    }

    GAME_LOG(Log, TEXT("【静态数据子系统】本批次数据来源：.bin 通道（%s）。"), *InBinPath);
    GAME_LOG(Log, TEXT("【静态数据装载耗时】.bin 通道反序列化 %.3f ms。"), (FPlatformTime::Seconds() - LoadStartSeconds) * 1000.0);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 打包产物启动自检（CI 冷启动双通道对拍用）
 * @param Subsystem 已完成冷启动装载的静态数据子系统
 */
void VerifyOnLaunchIfRequested(const UStaticDataSubsystem& Subsystem) {
    if (!FParse::Param(FCommandLine::Get(), TEXT("GameStaticDataVerify"))) {
        return;
    }

    const FStaticDataContentDigest Digest = Subsystem.BuildLoadedContentDigest();
    const bool bLoaded = Subsystem.IsAllDataLoaded() && Digest.TableCount > 0;
    GAME_LOG(Log, TEXT("表数=%d 行数=%d 内容摘要=%s"), Digest.TableCount, Digest.RowCount, *Digest.Digest);
    FFileHelper::SaveStringToFile(
        FString::Printf(TEXT("loaded=%d tables=%d rows=%d digest=%s"),
            bLoaded ? 1 : 0, Digest.TableCount, Digest.RowCount, *Digest.Digest),
        *(FPaths::ProjectSavedDir() / TEXT("StaticDataVerify.txt")));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bLoaded](float) {
        FPlatformMisc::RequestExitWithStatus(false, bLoaded ? 0 : 1);
        return false;
    }));
}

}  // namespace

/* -------------------------------------------------------------------------
 * Public 成员函数实现（按头文件声明顺序 1:1 对齐）
 * ------------------------------------------------------------------------- */

/**
 * @brief 子系统初始化生命周期回调：执行冷启动同步加载及可选启动自检
 * @param Collection 子系统依赖集合
 */
void UStaticDataSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    LifetimeCancelSource = FCancellationTokenSource();
    (void)LoadAllStaticData(false);
    VerifyOnLaunchIfRequested(*this);
}

/**
 * @brief 子系统反初始化生命周期回调：取消在途异步预热并释放重载门闩
 */
void UStaticDataSubsystem::Deinitialize() {
    LifetimeCancelSource.Cancel();
    SnapshotStore.EndReload();
    Super::Deinitialize();
}

/**
 * @brief 加载所有域的静态数据容器（走统一提交流程）
 * @param bAsync 是否异步执行（冷启动默认为 false 确保就绪，热重载为 true）
 * @return 暂存数据加载并进入提交流程返回 Success，失败返回 Failure
 */
TGameStatusResult<void> UStaticDataSubsystem::LoadAllStaticData(bool bAsync) {
    return BeginReloadFlow(ResolvePreferBinChannel(), bAsync);
}

/**
 * @brief 查询静态数据是否已全部加载成功
 * @return 若全部分表数据均加载完成返回 true，否则返回 false
 */
bool UStaticDataSubsystem::IsAllDataLoaded() const {
    return bIsAllDataLoaded;
}

/**
 * @brief 重新加载全部静态数据（恒走后台异步预热，在线数据零触碰；成功后版本号自增并广播）
 * @return 暂存数据加载并进入后台预热返回 Success，失败返回 Failure
 */
TGameStatusResult<void> UStaticDataSubsystem::ReloadAllStaticData() {
    return BeginReloadFlow(false, true);
}

/**
 * @brief 获取当前数据版本号
 * @return 当前版本号（自 0 起，每次成功提交自增）
 */
uint32 UStaticDataSubsystem::GetDataVersion() const noexcept {
    return SnapshotStore.GetVersion();
}

/**
 * @brief 获取当前在线快照（读者持有期间内容绝对稳定，不随后续提交改变）
 * @return 快照共享指针，尚未完成首次加载返回空
 */
TSharedPtr<const FStaticDataSnapshot> UStaticDataSubsystem::GetSnapshot() const {
    return SnapshotStore.Acquire();
}

/**
 * @brief 获取数据版本观察者
 * @return 观察者引用（订阅后提交时收到新版本号）
 */
FValueObserver<uint32>& UStaticDataSubsystem::GetDataVersionObserver() {
    return DataVersionObserver;
}

/**
 * @brief 汇总当前在线快照的数据内容摘要（Cold-Start 基准与双通道数据对拍用）
 * @return 表数、行数与顺序无关摘要；尚未完成首次加载返回零值结构
 *
 * 实现思路：取在线快照各域数据经「登记读写器 + 仓库」重建类型擦除镜像，
 * 再由仓库按结构名/行键排序计算摘要——与装载通道无关，仅反映数据内容。
 */
FStaticDataContentDigest UStaticDataSubsystem::BuildLoadedContentDigest() const {
    const TSharedPtr<const FStaticDataSnapshot> Snapshot = GetSnapshot();
    if (!Snapshot.IsValid()) {
        return FStaticDataContentDigest();
    }

    FStaticDataRepository Repository;
    RegisterGameStaticDataRowSerializers(Repository);

    if (const TGameStatusResult<void> AddRes_Item = Snapshot->ItemData.AddAllToRepository(Repository); AddRes_Item.IsFailure()) {
        GAME_LOG(Warning, TEXT("【静态数据校验】内容摘要构建失败：仓库写入失败（错误码 %d: %s）。"), AddRes_Item.GetError().ErrorCode, *AddRes_Item.GetError().Message);
        return FStaticDataContentDigest();
    }
    if (const TGameStatusResult<void> AddRes_Shop = Snapshot->ShopData.AddAllToRepository(Repository); AddRes_Shop.IsFailure()) {
        GAME_LOG(Warning, TEXT("【静态数据校验】内容摘要构建失败：仓库写入失败（错误码 %d: %s）。"), AddRes_Shop.GetError().ErrorCode, *AddRes_Shop.GetError().Message);
        return FStaticDataContentDigest();
    }

    return Repository.ComputeContentDigest();
}

/* -------------------------------------------------------------------------
 * Public 静态函数实现
 * ------------------------------------------------------------------------- */

/**
 * @brief 静态工具方法：安全获取当前静态数据子系统指针
 * @param InWorldContextObject 世界上下文对象
 * @return 静态数据子系统指针，不存在返回 nullptr
 */
UStaticDataSubsystem* UStaticDataSubsystem::GetPtr(const UObject* InWorldContextObject) {
    if (InWorldContextObject == nullptr) {
        return nullptr;
    }

    if (const UGameInstance* AsGameInstance = Cast<UGameInstance>(InWorldContextObject)) {
        return AsGameInstance->GetSubsystem<UStaticDataSubsystem>();
    }

    if (const UGameInstanceSubsystem* AsSubsystem = Cast<UGameInstanceSubsystem>(InWorldContextObject)) {
        const UGameInstance* OwningInstance = AsSubsystem->GetGameInstance();
        return OwningInstance ? OwningInstance->GetSubsystem<UStaticDataSubsystem>() : nullptr;
    }

    const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(InWorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (World == nullptr) {
        return nullptr;
    }

    const UGameInstance* GameInstance = World->GetGameInstance();
    return GameInstance ? GameInstance->GetSubsystem<UStaticDataSubsystem>() : nullptr;
}

/**
 * @brief 静态工具方法：获取当前静态数据子系统强引用
 * @param InWorldContextObject 世界上下文对象
 * @return 静态数据子系统引用
 */
UStaticDataSubsystem& UStaticDataSubsystem::Get(const UObject* InWorldContextObject) {
    UStaticDataSubsystem* Subsystem = GetPtr(InWorldContextObject);
    checkf(Subsystem != nullptr, TEXT("UStaticDataSubsystem::Get: 无法从给定上下文获取静态数据子系统实例。"));
    return *Subsystem;
}

/**
 * @brief 静态工具方法：向全部在线世界上下文派发静态数据重载
 */
void UStaticDataSubsystem::ReloadAllWorldStaticData() {
    if (GEngine == nullptr) {
        return;
    }

    for (const FWorldContext& WorldContext : GEngine->GetWorldContexts()) {
        UGameInstance* GameInstance = WorldContext.OwningGameInstance.Get();
        if (GameInstance == nullptr) {
            continue;
        }
        if (UStaticDataSubsystem* Subsystem = GameInstance->GetSubsystem<UStaticDataSubsystem>()) {
            (void)Subsystem->ReloadAllStaticData();
        }
    }
}

/**
 * @brief 将 DataTables_Json 全量导出为运行期 .bin（Cook 前置；控制台命令 Game.ExportStaticDataBin 调用）
 * @return 成功返回 Success，失败返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<void> UStaticDataSubsystem::ExportStaticDataBin() {
    if constexpr (!Game::Config::bIsEditor) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::ExportDisabledInNonEditor,
            TEXT("静态数据 .bin 导出仅在编辑器构建中可用")
        );
    }

    FItemStaticData ExportItemData;
    FShopStaticData ExportShopData;

    const TGameStatusResult<void> JsonLoadResult = LoadStagingFromJson(ExportItemData, ExportShopData);
    if (JsonLoadResult.IsFailure()) {
        GAME_LOG(Error, TEXT("【静态数据二进制导出】失败：JSON 通道装载失败（错误码 %d: %s）。"), JsonLoadResult.GetError().ErrorCode, *JsonLoadResult.GetError().Message);
        return JsonLoadResult;
    }

    FStaticDataRepository Repository;
    RegisterGameStaticDataRowSerializers(Repository);

    if (const TGameStatusResult<void> AddRes_Item = ExportItemData.AddAllToRepository(Repository); AddRes_Item.IsFailure()) {
        GAME_LOG(Error, TEXT("【静态数据二进制导出】失败：仓库写入失败（错误码 %d: %s）。"), AddRes_Item.GetError().ErrorCode, *AddRes_Item.GetError().Message);
        return AddRes_Item;
    }
    if (const TGameStatusResult<void> AddRes_Shop = ExportShopData.AddAllToRepository(Repository); AddRes_Shop.IsFailure()) {
        GAME_LOG(Error, TEXT("【静态数据二进制导出】失败：仓库写入失败（错误码 %d: %s）。"), AddRes_Shop.GetError().ErrorCode, *AddRes_Shop.GetError().Message);
        return AddRes_Shop;
    }

    const FString BinPath = GetStaticDataBinPath();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(BinPath), true);
    const TGameStatusResult<void> ExportResult = Repository.ExportToFile(BinPath);
    if (ExportResult.IsFailure()) {
        GAME_LOG(Error, TEXT("【静态数据二进制导出】失败：写盘失败（错误码 %d: %s）: %s"), ExportResult.GetError().ErrorCode, *ExportResult.GetError().Message, *BinPath);
        return ExportResult;
    }

    GAME_LOG(Log, TEXT("【静态数据二进制导出】成功：已导出 %d 张表至 %s。"), Repository.TableCount(), *BinPath);
    return TGameStatusResult<void>::Success();
}

/* -------------------------------------------------------------------------
 * Private 成员函数实现
 * ------------------------------------------------------------------------- */

/**
 * @brief 启动一次数据提交流程（支持同步冷启动与异步热更）
 * @param bPreferBin 是否优先走 .bin 通道
 * @param bAsync 是否异步执行（冷启动为 false 消除竞态；热更恒为 true）
 * @return 成功返回 Success，失败返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<void> UStaticDataSubsystem::BeginReloadFlow(bool bPreferBin, bool bAsync) {
    const TGameStatusResult<void> GateResult = SnapshotStore.TryBeginReload();
    if (GateResult.IsFailure()) {
        GAME_LOG(Warning, TEXT("【静态数据子系统】已有重载进行中，本次请求已忽略。"));
        return GateResult;
    }

    TSharedRef<FStaticDataSnapshotStore::FReloadLatchTicket, ESPMode::ThreadSafe> ReloadTicket = SnapshotStore.MakeReloadLatchTicket();
    auto ReloadGuard = MakeCancelableScopeGuard([ReloadTicket]() {
        ReloadTicket->Release();
    });

    const uint32 NextVersion = SnapshotStore.GetVersion() + 1;
    const FString BinPath = GetStaticDataBinPath();
    TSharedPtr<FStaticDataSnapshot> NewSnapshot = MakeShared<FStaticDataSnapshot>();
    NewSnapshot->DataVersion = NextVersion;

    bool bStagingLoaded = bPreferBin && TryLoadStagingFromBin(BinPath, NewSnapshot->ItemData, NewSnapshot->ShopData).IsSuccess();
    if (!bStagingLoaded) {
        const TGameStatusResult<void> JsonResult = LoadStagingFromJson(NewSnapshot->ItemData, NewSnapshot->ShopData);
        if (JsonResult.IsFailure()) {
            GAME_LOG(Warning, TEXT("【静态数据子系统】静态数据加载失败（错误码 %d: %s），在线数据保持原状。"), JsonResult.GetError().ErrorCode, *JsonResult.GetError().Message);
            return TGameStatusResult<void>::Failure(
                Game::ModuleId::Data,
                Game::DataErrorCodes::StagingLoadFailed,
                JsonResult.GetError().Message
            );
        }
    }

    if (!bAsync) {
        FItemStaticDataInterface::WarmUpCompositeCache(NewSnapshot->Cache, NewSnapshot->ItemData);
        FShopStaticDataInterface::WarmUpCompositeCache(NewSnapshot->Cache, NewSnapshot->ShopData);
        ReloadGuard.Dismiss();
        CommitSnapshot(MoveTemp(NewSnapshot));
        ReloadTicket->Release();
        return TGameStatusResult<void>::Success();
    }

    TWeakObjectPtr<UStaticDataSubsystem> WeakThis(this);
    const FCancellationToken LifetimeToken = LifetimeCancelSource.GetToken();
    ReloadGuard.Dismiss();
    FGameThreadTaskRunner::Run(WeakThis, LifetimeToken, [WeakThis, NewSnapshot, LifetimeToken, ReloadTicket]() {
        if (LifetimeToken.IsCancellationRequested()) {
            ReloadTicket->Release();
            return;
        }
        FItemStaticDataInterface::WarmUpCompositeCache(NewSnapshot->Cache, NewSnapshot->ItemData);
        FShopStaticDataInterface::WarmUpCompositeCache(NewSnapshot->Cache, NewSnapshot->ShopData);

        FGameThreadTaskRunner::RunOnGameThread(WeakThis, [WeakThis, NewSnapshot, LifetimeToken, ReloadTicket]() {
            if (LifetimeToken.IsCancellationRequested()) {
                ReloadTicket->Release();
                return;
            }
            WeakThis->CommitSnapshot(NewSnapshot);
            ReloadTicket->Release();
        });
    });
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 原子提交新快照（发布 + 释放门闩 + 广播，零主线程深拷贝）
 * @param InNextSnapshot 已完成预热的新快照（调用链保证非空）
 */
void UStaticDataSubsystem::CommitSnapshot(TSharedPtr<const FStaticDataSnapshot> InNextSnapshot) {
    const uint32 NewVersion = InNextSnapshot->DataVersion;
    bIsAllDataLoaded = true;
    SnapshotStore.Publish(MoveTemp(InNextSnapshot));
    SnapshotStore.EndReload();
    GAME_LOG(Log, TEXT("【静态数据子系统】静态数据提交成功，当前数据版本号: %u。"), NewVersion);
    DataVersionObserver.Set(NewVersion);
    OnStaticDataReloaded.Broadcast(this, NewVersion);
}
