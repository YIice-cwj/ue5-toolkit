/**
 * @file USaveGameSubsystem.h
 * @brief 通用存档与持久化调度子系统（高层调度门面 · 零业务字段 · 模块化子系统组合）
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Save/GameSaveArchive.h"
#include "Save/FSaveGameFileIO.h"
#include "Save/FSaveGameSegmentRegistry.h"

#include "USaveGameSubsystem.generated.h"

/**
 * @brief 异步保存完成动态多播委托
 * @param SlotName 存档槽位名称
 * @param bSuccess 是否保存成功
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAsyncSaveComplete, const FString&, SlotName, bool, bSuccess);

/**
 * @brief 异步加载完成动态多播委托
 * @param SlotName 存档槽位名称
 * @param bSuccess 是否加载成功
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAsyncLoadComplete, const FString&, SlotName, bool, bSuccess);

/**
 * @brief 通用存档与持久化调度子系统
 */
UCLASS(BlueprintType)
class GAMEDATACORE_API USaveGameSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    /**
     * @brief 构造函数：初始化默认分段归档类型为 UGameSaveArchive
     */
    USaveGameSubsystem();

    /**
     * @brief 获取指定槽位的正式文件绝对路径
     * @param SlotName 存档槽位名称
     * @return 正式存档文件绝对路径
     */
    static FString GetSaveSlotFilePath(const FString& SlotName);

    /**
     * @brief 获取指定槽位的临时写入文件绝对路径（.sav.tmp）
     * @param SlotName 存档槽位名称
     * @return 临时写入文件绝对路径
     */
    static FString GetSaveSlotTempFilePath(const FString& SlotName);

    /**
     * @brief 异步保存完成事件多播广播
     */
    UPROPERTY(BlueprintAssignable, Category = "GameData|Save")
    FOnAsyncSaveComplete OnSaveComplete;

    /**
     * @brief 异步加载完成事件多播广播
     */
    UPROPERTY(BlueprintAssignable, Category = "GameData|Save")
    FOnAsyncLoadComplete OnLoadComplete;

    /**
     * @brief 子系统销毁生命周期回调：清空分段注册表与白名单集合
     */
    void Deinitialize() override;

    /**
     * @brief 设置自定义的分段归档类型（继承自 UGameSaveArchive）
     * @param InSaveGameClass 目标归档类
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    void SetSaveGameClass(TSubclassOf<UGameSaveArchive> InSaveGameClass);

    /**
     * @brief 获取当前生效的分段归档类型
     * @return 当前归档类
     */
    UFUNCTION(BlueprintPure, Category = "GameData|Save")
    TSubclassOf<UGameSaveArchive> GetSaveGameClass() const;

    /**
     * @brief 设置允许使用的槽位名称白名单列表
     * @param InAllowedSlotNames 允许使用的槽位名称列表
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    void SetAllowedSlotNames(const TArray<FString>& InAllowedSlotNames);

    /**
     * @brief 判断槽位名称是否允许使用（符合白名单且路径字符合法）
     * @param SlotName 待检测的槽位名称
     * @return 允许使用返回 true，否则返回 false
     */
    UFUNCTION(BlueprintPure, Category = "GameData|Save")
    bool IsSlotNameAllowed(const FString& SlotName) const;

    /**
     * @brief 同步保存全部已注册段至指定槽位
     * @param SlotName 目标存档槽位名称
     * @return 保存成功返回 true，发生异常或被白名单拦截返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool SaveGameToSlot(const FString& SlotName);

    /**
     * @brief 异步保存全部已注册段至指定槽位（后台线程序列化与文件写入，完成后回主线程广播）
     * @param SlotName 目标存档槽位名称
     * @return 成功派发异步任务返回 true，被白名单拦截返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool SaveGameToSlotAsync(const FString& SlotName);

    /**
     * @brief 同步从指定槽位读取分段归档并逐段应用至已注册段
     * @param SlotName 目标存档槽位名称
     * @return 加载与反序列化成功返回 true，槽位不存在或数据损坏返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool LoadGameFromSlot(const FString& SlotName);

    /**
     * @brief 异步从指定槽位读取分段归档（后台线程读取文件，完成后回主线程反序列化与广播）
     * @param SlotName 目标存档槽位名称
     * @return 成功派发异步任务返回 true，被白名单拦截返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool LoadGameFromSlotAsync(const FString& SlotName);

    /**
     * @brief 将任意 USaveGame 对象同步保存至指定槽位（标准原子落盘）
     * @param SlotName 目标存档槽位名称
     * @param SaveGameObject 待保存的 USaveGame 对象指针
     * @return 保存成功返回 true，否则返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool SaveObjectToSlot(const FString& SlotName, USaveGame* SaveGameObject);

    /**
     * @brief 从指定槽位同步加载任意派生自 USaveGame 的对象
     * @param SlotName 目标存档槽位名称
     * @param SaveGameClassToLoad 目标加载的 USaveGame 类别
     * @param OutSaveGame 输出加载并反序列化完成的对象引用
     * @return 加载成功返回 true，否则返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool LoadObjectFromSlot(const FString& SlotName, TSubclassOf<USaveGame> SaveGameClassToLoad, USaveGame*& OutSaveGame);

    /**
     * @brief 检查指定槽位的存档文件是否存在
     * @param SlotName 目标存档槽位名称
     * @return 存在任意有效存档文件返回 true，否则返回 false
     */
    UFUNCTION(BlueprintPure, Category = "GameData|Save")
    bool DoesSaveSlotExist(const FString& SlotName) const;

    /**
     * @brief 彻底删除指定槽位的存档物理文件
     * @param SlotName 目标存档槽位名称
     * @return 删除成功返回 true，未找到或删除失败返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool DeleteSaveSlot(const FString& SlotName);

    /**
     * @brief 注册（或覆盖）具名存档段契约
     * @param Segment 待注册的存档段契约
     */
    void RegisterSegment(const FGameSaveSegment& Segment);

    /**
     * @brief 注销指定名称的存档段契约
     * @param SegmentName 目标存档段名称
     * @return 成功注销返回 true，未找到返回 false
     */
    bool UnregisterSegment(FName SegmentName);

    /**
     * @brief 查询指定名称的存档段是否已注册
     * @param SegmentName 目标存档段名称
     * @return 已注册返回 true，否则返回 false
     */
    [[nodiscard]] bool HasSegment(FName SegmentName) const;

private:
    UPROPERTY()
    TSubclassOf<UGameSaveArchive> SaveGameClass;

    TSet<FString> AllowedSlotNames;
    FSaveGameSegmentRegistry SegmentRegistry;
};
