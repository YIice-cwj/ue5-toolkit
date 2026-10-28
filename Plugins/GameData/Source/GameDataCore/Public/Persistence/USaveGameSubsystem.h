/**
 * @file USaveGameSubsystem.h
 * @brief 通用存档与持久化调度子系统（零业务字段）
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Persistence/GameSaveArchive.h"

#include "USaveGameSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAsyncSaveComplete, const FString&, SlotName, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAsyncLoadComplete, const FString&, SlotName, bool, bSuccess);

/**
 * @brief 通用存档与持久化调度子系统（零业务字段）
 *
 * 核心特性：
 * 1. 防断电损坏原子写保护：先写至 .sav.tmp，成功后再原子替换正式 .sav 归档；
 * 2. 真异步调度：主线程仅捕获只读数据快照，磁盘 I/O 全程在后台线程执行；
 * 3. 容灾恢复：若正式存档意外缺失但存在合法的 .sav.tmp，自动执行兜底恢复；
 * 4. 存档段注册：业务方按段注册保存/加载委托，子系统不感知任何业务字段；
 * 5. 版本迁移与槽位白名单：落盘段版本交由业务方迁移，槽位名可限制为白名单。
 */
UCLASS(BlueprintType)
class GAMEDATACORE_API USaveGameSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    /** 构造：默认分段归档类型设为 UGameSaveArchive */
    USaveGameSubsystem();

    /**
     * @brief 获取指定槽位的正式文件绝对路径
     * @param SlotName 存档槽位名称
     * @return 路径文本
     */
    static FString GetSaveSlotFilePath(const FString& SlotName);

    /**
     * @brief 获取指定槽位的临时写入文件绝对路径
     * @param SlotName 存档槽位名称
     * @return 路径文本
     */
    static FString GetSaveSlotTempFilePath(const FString& SlotName);

    /** 异步保存完成事件广播（参数：槽位名、是否成功） */
    UPROPERTY(BlueprintAssignable, Category = "GameData|Save")
    FOnAsyncSaveComplete OnSaveComplete;

    /** 异步加载完成事件广播（参数：槽位名、是否成功） */
    UPROPERTY(BlueprintAssignable, Category = "GameData|Save")
    FOnAsyncLoadComplete OnLoadComplete;

    /** 清空已注册段、未知段缓存与槽位白名单 */
    void Deinitialize() override;

    /**
     * @brief 设置分段归档类型（须为 UGameSaveArchive 派生类，可追加自定义 UPROPERTY 字段）
     * @param InSaveGameClass 分段归档类，nullptr 忽略
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    void SetSaveGameClass(TSubclassOf<UGameSaveArchive> InSaveGameClass);

    /**
     * @brief 获取当前分段归档类型
     * @return 分段归档类
     */
    UFUNCTION(BlueprintPure, Category = "GameData|Save")
    TSubclassOf<UGameSaveArchive> GetSaveGameClass() const;

    /**
     * @brief 设置槽位名白名单（空集合表示不限制，仅执行基础合法性校验）
     * @param InAllowedSlotNames 允许使用的槽位名清单
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    void SetAllowedSlotNames(const TArray<FString>& InAllowedSlotNames);

    /**
     * @brief 判断槽位名是否允许使用（基础合法性 + 白名单）
     * @param SlotName 存档槽位名称
     * @return 允许返回 true
     *
     * 实现思路：拒绝空名、超长名与路径分隔符/点号，杜绝越出 SaveGames 目录；
     * 白名单非空时还须命中白名单。
     */
    UFUNCTION(BlueprintPure, Category = "GameData|Save")
    bool IsSlotNameAllowed(const FString& SlotName) const;

    /**
     * @brief 同步保存全部已注册段至指定槽位（带原子写保护）
     * @param SlotName 存档槽位名称
     * @return 写盘是否成功
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool SaveGameToSlot(const FString& SlotName);

    /**
     * @brief 异步保存全部已注册段至指定槽位（主线程捕获段字节，后台落盘）
     * @param SlotName 存档槽位名称
     * @return 是否已成功派发后台任务
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool SaveGameToSlotAsync(const FString& SlotName);

    /**
     * @brief 同步从指定槽位读取分段归档并逐段应用
     * @param SlotName 存档槽位名称
     * @return 加载是否成功
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool LoadGameFromSlot(const FString& SlotName);

    /**
     * @brief 异步从指定槽位读取（后台读盘，回主线程反序列化并逐段应用）
     * @param SlotName 存档槽位名称
     * @return 是否已成功派发后台任务
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool LoadGameFromSlotAsync(const FString& SlotName);

    /**
     * @brief 将任意 USaveGame 对象同步保存至指定槽位（自定义存档类逃生口，带原子写保护）
     * @param SlotName 存档槽位名称
     * @param SaveGameObject 待保存对象
     * @return 写盘是否成功
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool SaveObjectToSlot(const FString& SlotName, USaveGame* SaveGameObject);

    /**
     * @brief 从指定槽位同步加载任意 USaveGame 对象（自定义存档类逃生口）
     * @param SlotName 存档槽位名称
     * @param SaveGameClassToLoad 期望的存档类
     * @param OutSaveGame 输出加载成功的对象
     * @return 加载成功且类型匹配返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool LoadObjectFromSlot(const FString& SlotName, TSubclassOf<USaveGame> SaveGameClassToLoad, USaveGame*& OutSaveGame);

    /**
     * @brief 检查指定槽位存档是否存在（正式 .sav 或临时 .sav.tmp 任一存在即算存在）
     * @param SlotName 存档槽位名称
     * @return 存在返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameData|Save")
    bool DoesSaveSlotExist(const FString& SlotName) const;

    /**
     * @brief 删除指定槽位存档（包括清理残留的 .sav.tmp）
     * @param SlotName 存档槽位名称
     * @return 删除是否成功
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool DeleteSaveSlot(const FString& SlotName);

    /**
     * @brief 注册（或按段名覆盖）一个存档段
     * @param Segment 存档段契约（段名、段版本、保存/加载委托）
     *
     * 实现思路：段名或任一委托为空则记录错误并拒绝注册。
     */
    void RegisterSegment(const FGameSaveSegment& Segment);

    /**
     * @brief 注销指定存档段
     * @param SegmentName 段名
     * @return 是否确实移除了已注册段
     */
    bool UnregisterSegment(FName SegmentName);

    /**
     * @brief 查询指定存档段是否已注册
     * @param SegmentName 段名
     * @return 已注册返回 true
     */
    [[nodiscard]] bool HasSegment(FName SegmentName) const;

private:
    /**
     * @brief 按当前注册段构建分段归档（主线程捕获段字节）
     * @param SlotName 槽位名
     * @param OutArchive 输出的分段归档
     * @return 全部分段捕获成功返回 true
     */
    bool BuildArchive(const FString& SlotName, UGameSaveArchive& OutArchive) const;

    /**
     * @brief 逐段应用已反序列化的分段归档（版本校验 + 迁移委托派发）
     * @param SlotName 实际加载的槽位名
     * @param Archive 已反序列化的分段归档
     * @return 全部分段应用成功返回 true
     */
    bool ApplyArchive(const FString& SlotName, const UGameSaveArchive& Archive);

    /**
     * @brief 将任意存档对象序列化为字节（主线程）
     * @param SaveGameObject 待序列化对象
     * @param OutBytes 输出字节
     * @return 序列化成功返回 true
     */
    static bool SerializeObject(USaveGame* SaveGameObject, TArray<uint8>& OutBytes);

    /**
     * @brief 原子写出字节至指定槽位（可脱离主线程安全调用）
     * @param SlotName 槽位名
     * @param Bytes 待写字节
     * @param SaveGeneration 保存代次序号（防止并发乱序覆盖）
     * @return 写盘并原子替换成功返回 true
     */
    static bool WriteSlotBytes(const FString& SlotName, const TArray<uint8>& Bytes, uint64 SaveGeneration = 0);

    /**
     * @brief 读取指定槽位字节（正式文件优先，缺失时自 .sav.tmp 抢救恢复）
     * @param SlotName 槽位名
     * @param OutBytes 输出字节
     * @return 读取成功返回 true
     */
    static bool ReadSlotBytes(const FString& SlotName, TArray<uint8>& OutBytes);

    /** 当前分段归档类（可由业务替换为派生类以追加自定义字段） */
    UPROPERTY()
    TSubclassOf<UGameSaveArchive> SaveGameClass;

    /** 已注册存档段（段名 -> 契约） */
    TMap<FName, FGameSaveSegment> Segments;

    /** 已从存档读取但当前尚未注册的未知段缓存（槽位名 -> (段名 -> 字节负载)） */
    TMap<FString, TMap<FName, FGameSaveSegmentPayload>> PreservedSegmentsBySlot;

    /** 槽位名白名单（空集合表示不限制） */
    TSet<FString> AllowedSlotNames;
};
