/**
 * @file USaveGameSubsystem.h
 * @brief 通用存档与持久化调度子系统（高层调度门面 · 零业务字段 · 模块化子系统组合）
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FCancellationTokenSource.h"
#include "Core/DataErrorCodes.h"
#include "Save/FSaveGameSegmentRegistry.h"
#include "Save/GameSaveArchive.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/TGameResult.h"

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
 * @param bSuccess 是否加载成功；过期请求被取消时为 false
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
     * @brief 子系统初始化生命周期回调：重置异步生命周期取消源
     * @param Collection 子系统依赖集合
     */
    void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 子系统销毁生命周期回调：取消在途异步任务并清空分段注册表、白名单集合与异步加载代次
     */
    void Deinitialize() override;

    /**
     * @brief 获取当前上下文的存档子系统指针（支持 UWorld、UGameInstance、UGameInstanceSubsystem 与 AActor 等任意上下文）
     * @param WorldContextObject 世界或游戏实例上下文对象
     * @return 存档子系统指针；未就绪时返回 nullptr
     */
    [[nodiscard]] static USaveGameSubsystem* GetPtr(const UObject* WorldContextObject);

    /**
     * @brief 获取当前上下文的存档子系统引用（断言子系统有效）
     * @param WorldContextObject 世界或游戏实例上下文对象
     * @return 存档子系统强引用
     */
    [[nodiscard]] static USaveGameSubsystem& Get(const UObject* WorldContextObject);

    /**
     * @brief 设置自定义的分段归档类型（继承自 UGameSaveArchive 且不得为抽象类）
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
     * @brief 同步保存全部已注册段至指定槽位（蓝图专用外观方法）
     * @param SlotName 目标存档槽位名称
     * @return 保存成功返回 true，发生异常或被白名单拦截返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save", meta = (DisplayName = "Save Game To Slot"))
    bool K2_SaveGameToSlot(const FString& SlotName);

    /**
     * @brief 同步保存全部已注册段至指定槽位
     * @param SlotName 目标存档槽位名称
     * @return 成功返回 Success，失败返回含 Game::DataErrorCodes 的 Failure
     */
    [[nodiscard]] TGameStatusResult<void> SaveGameToSlot(const FString& SlotName);

    /**
     * @brief 异步保存全部已注册段至指定槽位（主线程捕获与序列化，后台仅写文件，完成后回主线程广播）
     * @param SlotName 目标存档槽位名称
     * @return 成功派发异步任务返回 true，前置校验或序列化失败返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool SaveGameToSlotAsync(const FString& SlotName);

    /**
     * @brief 同步从指定槽位读取分段归档并逐段应用至已注册段（蓝图专用外观方法）
     * @param SlotName 目标存档槽位名称
     * @return 加载与反序列化成功返回 true，槽位不存在或数据损坏返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save", meta = (DisplayName = "Load Game From Slot"))
    bool K2_LoadGameFromSlot(const FString& SlotName);

    /**
     * @brief 同步从指定槽位读取分段归档并逐段应用至已注册段
     * @param SlotName 目标存档槽位名称
     * @return 成功返回 Success，失败返回含 Game::DataErrorCodes 的 Failure
     */
    [[nodiscard]] TGameStatusResult<void> LoadGameFromSlot(const FString& SlotName);

    /**
     * @brief 异步从指定槽位读取分段归档（后台读文件，主线程仅应用最新请求）
     * @param SlotName 目标存档槽位名称
     * @return 成功派发异步任务返回 true，被白名单拦截返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save")
    bool LoadGameFromSlotAsync(const FString& SlotName);

    /**
     * @brief 在内存中反序列化并校验字节流是否匹配当前归档类型、归档版本及全部已注册段版本（零磁盘 I/O 与零业务状态修改）
     * @param SlotName 目标存档槽位名称（用于诊断日志）
     * @param Bytes 待校验的完整 USaveGame 字节流
     * @return 校验通过返回反序列化后的归档对象指针，失败返回含具体 Game::DataErrorCodes 的 Failure
     */
    [[nodiscard]] TGameStatusResult<UGameSaveArchive*> DeserializeAndValidateArchive(
        const FString& SlotName,
        const TArray<uint8>& Bytes
    ) const;

    /**
     * @brief 将已校验通过的内存归档对象逐段应用至已注册段（消除落盘后二次读盘）
     * @param SlotName 目标存档槽位名称
     * @param Archive 已通过反序列化与版本校验的归档对象引用
     * @return 应用成功返回 Success，失败返回含 Game::DataErrorCodes 的 Failure
     */
    [[nodiscard]] TGameStatusResult<void> ApplyArchiveToSegments(
        const FString& SlotName,
        const UGameSaveArchive& Archive
    );

    /**
     * @brief 将任意 USaveGame 对象同步保存至指定槽位（蓝图专用外观方法）
     * @param SlotName 目标存档槽位名称
     * @param SaveGameObject 待保存的 USaveGame 对象指针
     * @return 保存成功返回 true，否则返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save", meta = (DisplayName = "Save Object To Slot"))
    bool K2_SaveObjectToSlot(const FString& SlotName, USaveGame* SaveGameObject);

    /**
     * @brief 将任意 USaveGame 对象同步保存至指定槽位（标准原子落盘）
     * @param SlotName 目标存档槽位名称
     * @param SaveGameObject 待保存的 USaveGame 对象指针
     * @return 成功返回 Success，失败返回含 Game::DataErrorCodes 的 Failure
     */
    [[nodiscard]] TGameStatusResult<void> SaveObjectToSlot(const FString& SlotName, USaveGame* SaveGameObject);

    /**
     * @brief 从指定槽位同步加载任意派生自 USaveGame 的对象（蓝图专用外观方法）
     * @param SlotName 目标存档槽位名称
     * @param SaveGameClassToLoad 目标加载的 USaveGame 类别
     * @param OutSaveGame 输出加载并反序列化完成的对象引用
     * @return 加载成功返回 true，否则返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save", meta = (DisplayName = "Load Object From Slot"))
    bool K2_LoadObjectFromSlot(const FString& SlotName, TSubclassOf<USaveGame> SaveGameClassToLoad, USaveGame*& OutSaveGame);

    /**
     * @brief 从指定槽位同步加载任意派生自 USaveGame 的对象
     * @param SlotName 目标存档槽位名称
     * @param SaveGameClassToLoad 目标加载的 USaveGame 类别
     * @return 成功返回加载对象的强类型指针，失败返回含 Game::DataErrorCodes 的 Failure
     */
    [[nodiscard]] TGameStatusResult<USaveGame*> LoadObjectFromSlot(const FString& SlotName, TSubclassOf<USaveGame> SaveGameClassToLoad);

    /**
     * @brief 从指定槽位同步加载指定类型的 USaveGame 对象（强类型模板版本）
     * @tparam TSaveGameClass 派生自 USaveGame 的目标存档类型
     * @param SlotName 目标存档槽位名称
     * @return 成功返回强类型指针，失败返回含 Game::DataErrorCodes 的 Failure
     */
    template <typename TSaveGameClass>
    [[nodiscard]] TGameStatusResult<TSaveGameClass*> LoadObjectFromSlot(const FString& SlotName);

    /**
     * @brief 检查指定槽位的存档文件是否存在
     * @param SlotName 目标存档槽位名称
     * @return 存在任意有效存档文件返回 true，否则返回 false
     */
    UFUNCTION(BlueprintPure, Category = "GameData|Save")
    bool DoesSaveSlotExist(const FString& SlotName) const;

    /**
     * @brief 彻底删除指定槽位的存档物理文件，并使该槽位在途异步加载失效（蓝图专用外观方法）
     * @param SlotName 目标存档槽位名称
     * @return 删除成功返回 true，未找到或删除失败返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Save", meta = (DisplayName = "Delete Save Slot"))
    bool K2_DeleteSaveSlot(const FString& SlotName);

    /**
     * @brief 彻底删除指定槽位的存档物理文件，并使该槽位在途异步加载失效
     * @param SlotName 目标存档槽位名称
     * @return 成功删除返回 Success，未找到或删除失败返回含 Game::DataErrorCodes 的 Failure
     */
    [[nodiscard]] TGameStatusResult<void> DeleteSaveSlot(const FString& SlotName);

    /**
     * @brief 注册（或覆盖）具名存档段契约
     * @param Segment 待注册的存档段契约
     * @return 注册成功返回 Success，参数非法返回 InvalidSegment 失败
     */
    [[nodiscard]] TGameStatusResult<void> RegisterSegment(const FGameSaveSegment& Segment);

    /**
     * @brief 注销指定名称的存档段契约
     * @param SegmentName 目标存档段名称
     * @return 成功注销返回 Success，未找到返回 SegmentNotFound 失败
     */
    [[nodiscard]] TGameStatusResult<void> UnregisterSegment(FName SegmentName);

    /**
     * @brief 查询指定名称的存档段是否已注册
     * @param SegmentName 目标存档段名称
     * @return 已注册返回 true，否则返回 false
     */
    [[nodiscard]] bool HasSegment(FName SegmentName) const;

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

private:
    /**
     * @brief 主线程组装全部已注册段并序列化为二进制字节流（同步与异步保存共享）
     * @param SlotName 目标存档槽位名称
     * @return 成功返回二进制字节流，失败返回含 Game::DataErrorCodes 的 Failure
     */
    [[nodiscard]] TGameStatusResult<TArray<uint8>> BuildAndSerializeArchive(const FString& SlotName) const;

    /**
     * @brief 主线程结算异步加载结果（校验代次最新性、反序列化并应用内存段、输出日志与广播事件）
     * @param SlotName 目标存档槽位名称
     * @param LoadGeneration 发起时签发的异步加载代次
     * @param ReadResult 后台读取结果
     */
    void CompleteAsyncLoad(
        const FString& SlotName,
        uint64 LoadGeneration,
        TGameStatusResult<TArray<uint8>> ReadResult
    );

    UPROPERTY()
    TSubclassOf<UGameSaveArchive> SaveGameClass;

    TSet<FString> AllowedSlotNames;
    FSaveGameSegmentRegistry SegmentRegistry;
    TMap<FString, uint64> AsyncLoadGenerations;
    FCancellationTokenSource LifetimeCancelSource;
};

template <typename TSaveGameClass>
TGameStatusResult<TSaveGameClass*> USaveGameSubsystem::LoadObjectFromSlot(const FString& SlotName) {
    static_assert(TIsDerivedFrom<TSaveGameClass, USaveGame>::Value, "TSaveGameClass must derive from USaveGame");
    TGameStatusResult<USaveGame*> BaseResult = LoadObjectFromSlot(SlotName, TSaveGameClass::StaticClass());
    if (BaseResult.IsFailure()) {
        return TGameStatusResult<TSaveGameClass*>::Failure(BaseResult.ExtractError());
    }
    TSaveGameClass* TypedObj = Cast<TSaveGameClass>(BaseResult.GetValue());
    if (TypedObj == nullptr) {
        return TGameStatusResult<TSaveGameClass*>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::SaveTypeMismatch,
            FString::Printf(TEXT("加载的存档对象类型不匹配目标类型 %s"), *TSaveGameClass::StaticClass()->GetName())
        );
    }
    return TGameStatusResult<TSaveGameClass*>::Success(TypedObj);
}
