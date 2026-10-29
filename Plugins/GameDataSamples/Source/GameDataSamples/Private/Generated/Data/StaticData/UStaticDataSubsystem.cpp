// Automatically generated global static data subsystem implementation. Do not edit manually.
#include "Generated/Data/StaticData/UStaticDataSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Tasks/FGameThreadTaskRunner.h"

#include "Config/GameCompileSwitches.h"
#include "StaticData/FStaticDataRepository.h"
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
 *
 * 实现思路：供 Cold-Start 基准与 CI 双通道对拍逐个通道取证使用，常规发布不指定该开关
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
 * @param OutItemData 等各域暂存容器（逐域装载，任一失败即整体判失败）
 * @return 全部域装载成功返回 true
 */
bool LoadStagingFromJson(FItemStaticData& OutItemData, FShopStaticData& OutShopData) {
    const double LoadStartSeconds = FPlatformTime::Seconds();
    bool bAllLoaded = true;
    bAllLoaded &= OutItemData.LoadAllFromJson();
    bAllLoaded &= OutShopData.LoadAllFromJson();

    if (!bAllLoaded) {
        return false;
    }
    GAME_LOG(Log, TEXT("【静态数据子系统】本批次数据来源：JSON 通道。"));
    GAME_LOG(Log, TEXT("【静态数据装载耗时】JSON 通道反序列化 %.3f ms。"), (FPlatformTime::Seconds() - LoadStartSeconds) * 1000.0);
    return true;
}

/**
 * @brief 尝试自 .bin 通道整体装载各域暂存数据（任一步失败即整体判失败，交由调用方回落 JSON）
 * @param InBinPath .bin 文件路径
 * @param OutItemData 等各域暂存容器（失败时不保证内容有效，调用方随后以 JSON 覆盖重载）
 * @return 全部表提取成功返回 true
 *
 * 实现思路：新建仓库并登记生成式读写器 → 装载 .bin（magic/版本/CRC 任一不符即整体拒绝）→
 * 逐域按表提取；失败分支输出错误码供回落诊断
 */
bool TryLoadStagingFromBin(const FString& InBinPath, FItemStaticData& OutItemData, FShopStaticData& OutShopData) {
    const double LoadStartSeconds = FPlatformTime::Seconds();
    FStaticDataRepository Repository;
    RegisterGameStaticDataRowSerializers(Repository);

    if (!Repository.ImportFromFile(InBinPath)) {
        GAME_LOG(Warning, TEXT("【静态数据子系统】.bin 通道装载失败（错误码 %d），已回落 JSON 通道。"), static_cast<int32>(Repository.GetLastError()));
        return false;
    }

    bool bAllExtracted = true;
    bAllExtracted &= OutItemData.LoadAllFromRepository(Repository);
    bAllExtracted &= OutShopData.LoadAllFromRepository(Repository);

    if (!bAllExtracted) {
        GAME_LOG(Warning, TEXT("【静态数据子系统】.bin 通道表提取失败（错误码 %d），已回落 JSON 通道。"), static_cast<int32>(Repository.GetLastError()));
        return false;
    }

    GAME_LOG(Log, TEXT("【静态数据子系统】本批次数据来源：.bin 通道（%s）。"), *InBinPath);
    GAME_LOG(Log, TEXT("【静态数据装载耗时】.bin 通道反序列化 %.3f ms。"), (FPlatformTime::Seconds() - LoadStartSeconds) * 1000.0);
    return true;
}

}  // namespace

/* -------------------------------------------------------------------------
 * Public 成员函数实现（按头文件声明顺序 1:1 对齐）
 * ------------------------------------------------------------------------- */

void UStaticDataSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    LoadAllStaticData(false);
}

void UStaticDataSubsystem::Deinitialize() {
    SnapshotStore.EndReload();
    Super::Deinitialize();
}

/**
 * @brief 加载所有域的静态数据容器（走统一提交流程）
 * @param bAsync 是否异步执行（冷启动默认为 false 确保就绪，热重载为 true）
 * @return 暂存数据是否加载成功并已就绪/进入后台预热
 */
bool UStaticDataSubsystem::LoadAllStaticData(bool bAsync) {
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
 * @return 暂存数据是否加载成功并已进入后台预热
 */
bool UStaticDataSubsystem::ReloadAllStaticData() {
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
 * 实现思路：取在线快照各域数据（Item/Shop）经「登记读写器 + 仓库」重建类型擦除镜像，
 * 再由仓库按结构名/行键排序计算摘要——与装载通道无关，仅反映数据内容
 */
FStaticDataContentDigest UStaticDataSubsystem::BuildLoadedContentDigest() const {
    const TSharedPtr<const FStaticDataSnapshot> Snapshot = GetSnapshot();
    if (!Snapshot.IsValid()) {
        return FStaticDataContentDigest();
    }

    FStaticDataRepository Repository;
    RegisterGameStaticDataRowSerializers(Repository);

    bool bAllAdded = true;
    bAllAdded &= Snapshot->ItemData.AddAllToRepository(Repository);
    bAllAdded &= Snapshot->ShopData.AddAllToRepository(Repository);
    if (!bAllAdded) {
        GAME_LOG(Warning, TEXT("【静态数据校验】内容摘要构建失败：仓库写入失败（错误码 %d）。"), static_cast<int32>(Repository.GetLastError()));
        return FStaticDataContentDigest();
    }

    return Repository.ComputeContentDigest();
}

/* -------------------------------------------------------------------------
 * Public 静态函数实现
 * ------------------------------------------------------------------------- */

/**
 * @brief 静态工具方法：通过任意上下文对象获取当前静态数据子系统
 * @param InWorldContextObject 世界上下文对象
 * @return 静态数据子系统指针，不存在返回 nullptr
 */
UStaticDataSubsystem* UStaticDataSubsystem::Get(const UObject* InWorldContextObject) {
    if (InWorldContextObject == nullptr) {
        return nullptr;
    }

    if (const UGameInstance* AsGameInstance = Cast<UGameInstance>(InWorldContextObject)) {
        return AsGameInstance->GetSubsystem<UStaticDataSubsystem>();
    }

    const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(InWorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (World == nullptr) {
        return nullptr;
    }

    UGameInstance* GameInstance = World->GetGameInstance();
    return GameInstance ? GameInstance->GetSubsystem<UStaticDataSubsystem>() : nullptr;
}

/**
 * @brief 静态工具方法：向全部在线世界上下文派发静态数据重载
 *
 * 实现思路：磁盘热更监听与控制台命令共用入口；每个 GameInstance 各持一份静态数据，
 * 按引擎世界上下文集合遍历派发，保证所有世界同批换新（版本号各自自增）
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
            Subsystem->ReloadAllStaticData();
        }
    }
}

/**
 * @brief 将 DataTables_Json 全量导出为运行期 .bin（Cook 前置；控制台命令 Game.ExportStaticDataBin 调用）
 * @return 导出成功返回 true（失败原因见日志与仓库错误码）
 *
 * 实现思路：编译期 constexpr 开关分支（非编辑器构建编译期剔除并返回 false）；
 * 逐域从 JSON 装载本地容器（与运行期同源）→ 登记生成式读写器 → 逐域写入仓库 →
 * 建目录后整体导出至内容目录下的 .bin（运行期冷启动优先读取该文件）
 */
bool UStaticDataSubsystem::ExportStaticDataBin() {
    if constexpr (!Game::Config::bIsEditor) {
        return false;
    }

    FItemStaticData ExportItemData;
    FShopStaticData ExportShopData;

    if (!LoadStagingFromJson(ExportItemData, ExportShopData)) {
        GAME_LOG(Error, TEXT("【静态数据二进制导出】失败：JSON 通道装载失败。"));
        return false;
    }

    FStaticDataRepository Repository;
    RegisterGameStaticDataRowSerializers(Repository);

    bool bAllAdded = true;
    bAllAdded &= ExportItemData.AddAllToRepository(Repository);
    bAllAdded &= ExportShopData.AddAllToRepository(Repository);
    if (!bAllAdded) {
        GAME_LOG(Error, TEXT("【静态数据二进制导出】失败：仓库写入失败（错误码 %d）。"), static_cast<int32>(Repository.GetLastError()));
        return false;
    }

    const FString BinPath = GetStaticDataBinPath();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(BinPath), true);
    if (!Repository.ExportToFile(BinPath)) {
        GAME_LOG(Error, TEXT("【静态数据二进制导出】失败：写盘失败（错误码 %d）: %s"), static_cast<int32>(Repository.GetLastError()), *BinPath);
        return false;
    }

    GAME_LOG(Log, TEXT("【静态数据二进制导出】成功：已导出 %d 张表至 %s。"), Repository.TableCount(), *BinPath);
    return true;
}

/* -------------------------------------------------------------------------
 * Private 成员函数实现
 * ------------------------------------------------------------------------- */

/**
 * @brief 启动一次数据提交流程（支持同步冷启动与异步热更）
 * @param bPreferBin 是否优先走 .bin 通道
 * @param bAsync 是否异步执行（冷启动为 false 消除竞态；热更恒为 true）
 * @return 暂存数据是否加载成功并已就绪/派发
 */
bool UStaticDataSubsystem::BeginReloadFlow(bool bPreferBin, bool bAsync) {
    if (!SnapshotStore.TryBeginReload()) {
        GAME_LOG(Warning, TEXT("【静态数据子系统】已有重载进行中，本次请求已忽略。"));
        return false;
    }

    const uint32 NextVersion = SnapshotStore.GetVersion() + 1;
    const FString BinPath = GetStaticDataBinPath();
    TWeakObjectPtr<UStaticDataSubsystem> WeakThis(this);

    TSharedPtr<FStaticDataSnapshot> NewSnapshot = MakeShared<FStaticDataSnapshot>();
    NewSnapshot->DataVersion = NextVersion;
    bool bStagingLoaded = bPreferBin && TryLoadStagingFromBin(BinPath, NewSnapshot->ItemData, NewSnapshot->ShopData);
    if (!bStagingLoaded) {
        bStagingLoaded = LoadStagingFromJson(NewSnapshot->ItemData, NewSnapshot->ShopData);
    }
    if (!bStagingLoaded) {
        GAME_LOG(Warning, TEXT("【静态数据子系统】静态数据加载失败，在线数据保持原状。"));
        SnapshotStore.EndReload();
        return false;
    }

    if (!bAsync) {
        FItemStaticDataInterface::WarmUpCompositeCache(NewSnapshot->Cache, NewSnapshot->ItemData);
        FShopStaticDataInterface::WarmUpCompositeCache(NewSnapshot->Cache, NewSnapshot->ShopData);
        CommitSnapshot(NewSnapshot);
        return true;
    }

    FGameThreadTaskRunner::Run(WeakThis, [WeakThis, NewSnapshot]() {
        FItemStaticDataInterface::WarmUpCompositeCache(NewSnapshot->Cache, NewSnapshot->ItemData);
        FShopStaticDataInterface::WarmUpCompositeCache(NewSnapshot->Cache, NewSnapshot->ShopData);

        FGameThreadTaskRunner::RunOnGameThread(WeakThis, [WeakThis, NewSnapshot]() {
            if (WeakThis.IsValid()) {
                WeakThis->CommitSnapshot(NewSnapshot);
            }
        });
    });
    return true;
}

/**
 * @brief 原子提交新快照（发布 + 广播 + 释放门闩，零主线程深拷贝）
 * @param InNextSnapshot 已完成预热的新快照（调用链保证非空）
 */
void UStaticDataSubsystem::CommitSnapshot(TSharedPtr<const FStaticDataSnapshot> InNextSnapshot) {
    const uint32 NewVersion = InNextSnapshot->DataVersion;
    bIsAllDataLoaded = true;
    SnapshotStore.Publish(MoveTemp(InNextSnapshot));
    GAME_LOG(Log, TEXT("【静态数据子系统】静态数据提交成功，当前数据版本号: %u。"), NewVersion);
    DataVersionObserver.Set(NewVersion);
    OnStaticDataReloaded.Broadcast(this, NewVersion);
    SnapshotStore.EndReload();
}
