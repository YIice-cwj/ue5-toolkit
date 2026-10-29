// Automatically generated global static data subsystem. Do not edit manually.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "Generated/Data/StaticData/StaticDataTypes.h"
#include "StaticData/FStaticDataRepository.h"
#include "Generated/Data/StaticData/FStaticDataSnapshot.h"
#include "Generated/Data/StaticData/FStaticDataSnapshotStore.h"
#include "Listener/FValueObserver.h"

#include "UStaticDataSubsystem.generated.h"

/**
 * @brief 静态数据重载完成事件委托（参数：触发重载的子系统指针、递增后的数据版本号）
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FStaticDataReloadedDelegate, UStaticDataSubsystem*, uint32)

/**
 * @brief 全局静态数据子系统（随数据表管线自动生成）
 *
 * 继承自 UGameInstanceSubsystem，作为全局静态配置数据仓库唯一持有者：
 * 以「快照所有权 + 原子发布」承载各域数据，首次加载与热重载共用同一条提交流程
 * （门闩防重入 → 主线程反序列化暂存 → 后台预热复合缓存 → 回主线程原子发布）。
 * 业务查询请先取 GetSnapshot()，再交各域 F{域}StaticDataInterface / F{表}Query。
 */
UCLASS(BlueprintType)
class GAMEDATASAMPLES_API UStaticDataSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;

    /**
     * @brief 加载所有域的静态数据容器（走统一提交流程）
     * @param bAsync 是否异步执行（冷启动默认为 false 确保就绪，热重载为 true）
     * @return 暂存数据是否加载成功并已就绪/进入后台预热
     *
     * 实现思路：冷启动优先读 .bin 通道，magic/版本/CRC 任一不符或表缺失即整体回落 JSON 通道；
     * 命令行 -GameStaticDataChannel=json 可强制 JSON 通道（Cold-Start 基准与双通道对拍用）
     */
    bool LoadAllStaticData(bool bAsync = false);

    /**
     * @brief 查询静态数据是否已全部加载成功
     * @return 若全部分表数据均加载完成返回 true，否则返回 false
     */
    UFUNCTION(BlueprintPure, Category = "Game|StaticData")
    bool IsAllDataLoaded() const;

    /**
     * @brief 重新加载全部静态数据（恒走后台异步预热，在线数据零触碰；成功后版本号自增并广播）
     * @return 暂存数据是否加载成功并已进入后台预热
     *
     * 实现思路：热更的数据源即磁盘 JSON，故直接走 JSON 通道，不经 .bin（避免读到落后于 JSON 的旧档）
     */
    bool ReloadAllStaticData();

    /**
     * @brief 获取当前数据版本号
     * @return 当前版本号（自 0 起，每次成功提交自增）
     */
    [[nodiscard]] uint32 GetDataVersion() const noexcept;

    /**
     * @brief 获取当前在线快照（读者持有期间内容绝对稳定，不随后续提交改变）
     * @return 快照共享指针，尚未完成首次加载返回空
     */
    [[nodiscard]] TSharedPtr<const FStaticDataSnapshot> GetSnapshot() const;

    /**
     * @brief 获取数据版本观察者（订阅静态数据更新，提交后收到新版本号）
     * @return 观察者引用
     */
    [[nodiscard]] FValueObserver<uint32>& GetDataVersionObserver();

    /**
     * @brief 汇总当前在线快照的数据内容摘要（Cold-Start 基准与双通道数据对拍用）
     * @return 表数、行数与顺序无关摘要；尚未完成首次加载返回零值结构
     *
     * 实现思路：取在线快照各域数据经「登记读写器 + 仓库」重建类型擦除镜像，
     * 再由仓库按结构名/行键排序计算摘要——与装载通道无关，仅反映数据内容
     */
    [[nodiscard]] FStaticDataContentDigest BuildLoadedContentDigest() const;

    /**
     * @brief 静态工具方法：通过任意上下文对象获取当前静态数据子系统
     * @param InWorldContextObject 世界上下文对象
     * @return 静态数据子系统指针，不存在返回 nullptr
     */
    UFUNCTION(BlueprintPure, Category = "Game|StaticData", meta = (WorldContext = "InWorldContextObject"))
    static UStaticDataSubsystem* Get(const UObject* InWorldContextObject);

    /**
     * @brief 静态工具方法：向全部在线世界上下文派发静态数据重载
     *
     * 实现思路：磁盘热更监听与控制台命令共用入口；每个 GameInstance 各持一份静态数据，
     * 按引擎世界上下文集合遍历派发，保证所有世界同批换新（版本号各自自增）
     */
    static void ReloadAllWorldStaticData();

    /**
     * @brief 将 DataTables_Json 全量导出为运行期 .bin（Cook 前置；控制台命令 Game.ExportStaticDataBin 调用）
     * @return 导出成功返回 true（失败原因见日志与仓库错误码）
     *
     * 实现思路：逐域从 JSON 装载本地容器（与运行期同源）→ 登记生成式读写器 → 逐域写入仓库 → 整体导出至内容目录
     */
    static bool ExportStaticDataBin();

    /**
     * @brief 静态数据重载完成事件（业务层可绑定此事件作废已缓存数据）
     */
    FStaticDataReloadedDelegate OnStaticDataReloaded;

private:
    /**
     * @brief 启动一次数据提交流程（支持同步冷启动与异步热更）
     * @param bPreferBin 是否优先走 .bin 通道
     * @param bAsync 是否异步执行（冷启动为 false 消除竞态；热更恒为 true）
     * @return 暂存数据是否加载成功并已就绪/派发
     */
    bool BeginReloadFlow(bool bPreferBin, bool bAsync);

    /**
     * @brief 原子提交新快照（发布 + 广播 + 释放门闩，零主线程深拷贝）
     * @param InNextSnapshot 已完成预热的新快照（调用链保证非空）
     */
    void CommitSnapshot(TSharedPtr<const FStaticDataSnapshot> InNextSnapshot);

    /**
     * @brief 是否已成功完成全量静态数据预加载（遵循 UE5 b 驼峰约定，UPROPERTY 无下划线尾缀）
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Game|StaticData", meta = (AllowPrivateAccess = "true"))
    bool bIsAllDataLoaded = false;

    /**
     * @brief 快照 RCU 存储（在线快照为查询唯一真源；重载门闩保证同一时刻仅一个写者）
     */
    FStaticDataSnapshotStore SnapshotStore;

    /**
     * @brief 数据版本号观察者（提交后 Set 广播，业务可订阅监听数据更新）
     */
    FValueObserver<uint32> DataVersionObserver{FName(TEXT("StaticData.DataVersion"))};
};
