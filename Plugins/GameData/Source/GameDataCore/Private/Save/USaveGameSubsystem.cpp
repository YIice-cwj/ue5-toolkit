/**
 * @file USaveGameSubsystem.cpp
 * @brief 通用存档与持久化调度子系统实现
 */

#include "Save/USaveGameSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Logging/GameLogCategories.h"
#include "Async/FGameThreadTaskRunner.h"

/**
 * @brief 构造函数：初始化默认分段归档类型为 UGameSaveArchive
 */
USaveGameSubsystem::USaveGameSubsystem()
    : SaveGameClass(UGameSaveArchive::StaticClass()) {
}

/**
 * @brief 获取当前上下文的存档子系统指针（支持 UWorld, UGameInstance, AActor 等任意上下文）
 * @param WorldContextObject 世界或游戏实例上下文对象
 * @return 存档子系统指针；未就绪时返回 nullptr
 */
USaveGameSubsystem* USaveGameSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr || GEngine == nullptr) {
        return nullptr;
    }
    if (const UGameInstance* DirectInstance = Cast<UGameInstance>(WorldContextObject)) {
        return DirectInstance->GetSubsystem<USaveGameSubsystem>();
    }
    if (const UGameInstanceSubsystem* Subsystem = Cast<UGameInstanceSubsystem>(WorldContextObject)) {
        if (const UGameInstance* GI = Subsystem->GetGameInstance()) {
            return GI->GetSubsystem<USaveGameSubsystem>();
        }
    }
    const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (World == nullptr || World->GetGameInstance() == nullptr) {
        return nullptr;
    }
    return World->GetGameInstance()->GetSubsystem<USaveGameSubsystem>();
}

/**
 * @brief 获取当前上下文的存档子系统引用（断言子系统有效）
 * @param WorldContextObject 世界或游戏实例上下文对象
 * @return 存档子系统强引用
 */
USaveGameSubsystem& USaveGameSubsystem::Get(const UObject* WorldContextObject) {
    USaveGameSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

/**
 * @brief 子系统销毁生命周期回调：清空分段注册表、白名单集合与异步加载代次
 */
void USaveGameSubsystem::Deinitialize() {
    SegmentRegistry.Reset();
    AllowedSlotNames.Empty();
    AsyncLoadGenerations.Empty();
    Super::Deinitialize();
}

/**
 * @brief 设置自定义的分段归档类型（继承自 UGameSaveArchive）
 * @param InSaveGameClass 目标归档类
 */
void USaveGameSubsystem::SetSaveGameClass(TSubclassOf<UGameSaveArchive> InSaveGameClass) {
    if (InSaveGameClass != nullptr) {
        SaveGameClass = InSaveGameClass;
    }
}

/**
 * @brief 获取当前生效的分段归档类型
 * @return 当前归档类
 */
TSubclassOf<UGameSaveArchive> USaveGameSubsystem::GetSaveGameClass() const {
    return SaveGameClass;
}

/**
 * @brief 设置允许使用的槽位名称白名单列表
 * @param InAllowedSlotNames 允许使用的槽位名称列表
 */
void USaveGameSubsystem::SetAllowedSlotNames(const TArray<FString>& InAllowedSlotNames) {
    AllowedSlotNames.Reset();
    AllowedSlotNames.Append(InAllowedSlotNames);
}

/**
 * @brief 判断槽位名称是否允许使用（符合白名单且路径字符合法）
 * @param SlotName 待检测的槽位名称
 * @return 允许使用返回 true，否则返回 false
 */
bool USaveGameSubsystem::IsSlotNameAllowed(const FString& SlotName) const {
    return FSaveGameFileIO::IsSlotNameAllowed(SlotName, AllowedSlotNames);
}

/**
 * @brief 同步保存全部已注册段至指定槽位（蓝图专用外观方法）
 * @param SlotName 目标存档槽位名称
 * @return 保存成功返回 true，发生异常或被白名单拦截返回 false
 */
bool USaveGameSubsystem::K2_SaveGameToSlot(const FString& SlotName) {
    return SaveGameToSlot(SlotName).IsSuccess();
}

/**
 * @brief 同步保存全部已注册段至指定槽位
 * @param SlotName 目标存档槽位名称
 * @return 成功返回 Success，失败返回 Failure
 */
TGameStatusResult<void> USaveGameSubsystem::SaveGameToSlot(const FString& SlotName) {
    if (!IsSlotNameAllowed(SlotName)) {
        GAME_LOG(Warning, TEXT("【存档子系统】槽位名不合法或不在白名单内: %s"), *SlotName);
        return TGameStatusResult<void>::Failure(1, FString::Printf(TEXT("槽位名称 '%s' 不在白名单中或非法"), *SlotName));
    }

    UGameSaveArchive* Archive = NewObject<UGameSaveArchive>(this, SaveGameClass);
    const auto BuildResult = SegmentRegistry.BuildArchive(SlotName, *Archive);
    if (BuildResult.IsFailure()) {
        return BuildResult;
    }

    auto SerializeResult = FSaveGameFileIO::SerializeObject(Archive);
    if (SerializeResult.IsFailure()) {
        GAME_LOG(Error, TEXT("【存档子系统】同步保存序列化失败: %s"), *SlotName);
        return TGameStatusResult<void>::Failure(2, SerializeResult.GetError().Message);
    }

    const uint64 SaveGeneration = FSaveGameFileIO::IssueSlotSaveGeneration(SlotName);
    const auto IoResult = FSaveGameFileIO::WriteSlotBytes(SlotName, SerializeResult.ExtractValue(), SaveGeneration);
    if (IoResult.IsSuccess()) {
        GAME_LOG(Log, TEXT("【存档子系统】同步保存槽位成功: %s"), *SlotName);
    } else {
        GAME_LOG(Error, TEXT("【存档子系统】同步保存槽位失败: %s"), *SlotName);
    }
    return IoResult;
}

/**
 * @brief 异步保存全部已注册段至指定槽位（主线程捕获与序列化，后台仅写文件，完成后回主线程广播）
 * @param SlotName 目标存档槽位名称
 * @return 成功派发异步任务返回 true，被白名单拦截返回 false
 */
bool USaveGameSubsystem::SaveGameToSlotAsync(const FString& SlotName) {
    if (!IsSlotNameAllowed(SlotName)) {
        GAME_LOG(Warning, TEXT("【存档子系统】槽位名不合法或不在白名单内: %s"), *SlotName);
        OnSaveComplete.Broadcast(SlotName, false);
        return false;
    }

    UGameSaveArchive* Archive = NewObject<UGameSaveArchive>(this, SaveGameClass);
    if (SegmentRegistry.BuildArchive(SlotName, *Archive).IsFailure()) {
        OnSaveComplete.Broadcast(SlotName, false);
        return false;
    }

    auto SerializeResult = FSaveGameFileIO::SerializeObject(Archive);
    if (SerializeResult.IsFailure()) {
        GAME_LOG(Error, TEXT("【存档子系统】内存序列化失败: %s"), *SlotName);
        OnSaveComplete.Broadcast(SlotName, false);
        return false;
    }

    const uint64 SaveGeneration = FSaveGameFileIO::IssueSlotSaveGeneration(SlotName);
    TWeakObjectPtr<USaveGameSubsystem> WeakThis(this);
    FGameThreadTaskRunner::Run(WeakThis, [WeakThis, SlotName, SaveGeneration, Payload = SerializeResult.ExtractValue()]() mutable {
        const bool bIoSuccess = FSaveGameFileIO::WriteSlotBytes(SlotName, Payload, SaveGeneration).IsSuccess();
        FGameThreadTaskRunner::RunOnGameThread(WeakThis, [WeakThis, SlotName, bIoSuccess]() {
            if (bIoSuccess) {
                GAME_LOG(Log, TEXT("【存档子系统】异步保存槽位成功: %s"), *SlotName);
            } else {
                GAME_LOG(Error, TEXT("【存档子系统】异步保存槽位失败: %s"), *SlotName);
            }
            WeakThis->OnSaveComplete.Broadcast(SlotName, bIoSuccess);
        });
    });

    return true;
}

/**
 * @brief 同步从指定槽位读取分段归档并逐段应用至已注册段（蓝图专用外观方法）
 * @param SlotName 目标存档槽位名称
 * @return 加载与反序列化成功返回 true，槽位不存在或数据损坏返回 false
 */
bool USaveGameSubsystem::K2_LoadGameFromSlot(const FString& SlotName) {
    return LoadGameFromSlot(SlotName).IsSuccess();
}

/**
 * @brief 同步从指定槽位读取分段归档并逐段应用至已注册段
 * @param SlotName 目标存档槽位名称
 * @return 成功返回 Success，失败返回 Failure
 */
TGameStatusResult<void> USaveGameSubsystem::LoadGameFromSlot(const FString& SlotName) {
    auto LoadResult = LoadObjectFromSlot(SlotName, SaveGameClass);
    if (LoadResult.IsFailure()) {
        GAME_LOG(Warning, TEXT("【存档子系统】同步装载槽位失败: %s"), *SlotName);
        return TGameStatusResult<void>::Failure(1, LoadResult.GetError().Message);
    }

    UGameSaveArchive* Archive = CastChecked<UGameSaveArchive>(LoadResult.GetValue());
    if (Archive->Metadata.ArchiveVersion > UGameSaveArchive::CurrentArchiveVersion) {
        GAME_LOG(Error, TEXT("【存档子系统】存档版本 %d 高于当前支持版本 %d，拒绝加载: %s"),
            Archive->Metadata.ArchiveVersion, UGameSaveArchive::CurrentArchiveVersion, *SlotName);
        return TGameStatusResult<void>::Failure(2, FString::Printf(TEXT("归档版本 %d 高于当前支持的版本 %d"),
            Archive->Metadata.ArchiveVersion, UGameSaveArchive::CurrentArchiveVersion));
    }

    const auto ApplyResult = SegmentRegistry.ApplyArchive(SlotName, *Archive);
    if (ApplyResult.IsFailure()) {
        GAME_LOG(Warning, TEXT("【存档子系统】同步装载槽位段应用失败: %s"), *SlotName);
        return ApplyResult;
    }

    GAME_LOG(Log, TEXT("【存档子系统】同步成功装载槽位: %s"), *SlotName);
    return TGameStatusResult<void>::Success();
}

/**
 * @brief 异步从指定槽位读取分段归档（后台读文件，主线程仅应用最新请求）
 * @param SlotName 目标存档槽位名称
 * @return 成功派发异步任务返回 true，被白名单拦截返回 false
 */
bool USaveGameSubsystem::LoadGameFromSlotAsync(const FString& SlotName) {
    if (!IsSlotNameAllowed(SlotName)) {
        GAME_LOG(Warning, TEXT("【存档子系统】槽位名不合法或不在白名单内: %s"), *SlotName);
        OnLoadComplete.Broadcast(SlotName, false);
        return false;
    }

    const uint64 LoadGeneration = ++AsyncLoadGenerations.FindOrAdd(SlotName);
    TWeakObjectPtr<USaveGameSubsystem> WeakThis(this);
    FGameThreadTaskRunner::Run(WeakThis, [WeakThis, SlotName, LoadGeneration]() {
        auto ReadResult = FSaveGameFileIO::ReadSlotBytes(SlotName);
        const bool bReadSuccess = ReadResult.IsSuccess();
        TArray<uint8> Bytes = bReadSuccess ? ReadResult.ExtractValue() : TArray<uint8>();

        FGameThreadTaskRunner::RunOnGameThread(WeakThis,
            [WeakThis, SlotName, LoadGeneration, bReadSuccess, Payload = MoveTemp(Bytes)]() mutable {
                const uint64* LatestGeneration = WeakThis->AsyncLoadGenerations.Find(SlotName);
                const bool bIsLatest = LatestGeneration != nullptr && *LatestGeneration == LoadGeneration;
                bool bSuccess = false;
                if (bIsLatest && bReadSuccess) {
                    USaveGame* LoadedObject = UGameplayStatics::LoadGameFromMemory(Payload);
                    if (LoadedObject != nullptr && LoadedObject->IsA(WeakThis->SaveGameClass)) {
                        UGameSaveArchive* Archive = CastChecked<UGameSaveArchive>(LoadedObject);
                        bSuccess = Archive->Metadata.ArchiveVersion <= UGameSaveArchive::CurrentArchiveVersion
                            && WeakThis->SegmentRegistry.ApplyArchive(SlotName, *Archive).IsSuccess();
                    }
                }

                if (!bIsLatest) {
                    GAME_LOG(Log, TEXT("【存档子系统】过期异步加载已取消: %s"), *SlotName);
                } else if (bSuccess) {
                    GAME_LOG(Log, TEXT("【存档子系统】异步成功装载槽位: %s"), *SlotName);
                } else {
                    GAME_LOG(Error, TEXT("【存档子系统】异步装载槽位失败: %s"), *SlotName);
                }
                WeakThis->OnLoadComplete.Broadcast(SlotName, bSuccess);
            });
    });

    return true;
}

/**
 * @brief 将任意 USaveGame 对象同步保存至指定槽位（蓝图专用外观方法）
 * @param SlotName 目标存档槽位名称
 * @param SaveGameObject 待保存的 USaveGame 对象指针
 * @return 保存成功返回 true，否则返回 false
 */
bool USaveGameSubsystem::K2_SaveObjectToSlot(const FString& SlotName, USaveGame* SaveGameObject) {
    return SaveObjectToSlot(SlotName, SaveGameObject).IsSuccess();
}

/**
 * @brief 将任意 USaveGame 对象同步保存至指定槽位（标准原子落盘）
 * @param SlotName 目标存档槽位名称
 * @param SaveGameObject 待保存的 USaveGame 对象指针
 * @return 成功返回 Success，失败返回 Failure
 */
TGameStatusResult<void> USaveGameSubsystem::SaveObjectToSlot(const FString& SlotName, USaveGame* SaveGameObject) {
    if (SaveGameObject == nullptr) {
        return TGameStatusResult<void>::Failure(1, TEXT("SaveGameObject 为空"));
    }
    if (!IsSlotNameAllowed(SlotName)) {
        return TGameStatusResult<void>::Failure(2, FString::Printf(TEXT("槽位名称 '%s' 不在白名单中或非法"), *SlotName));
    }

    auto SerializeResult = FSaveGameFileIO::SerializeObject(SaveGameObject);
    if (SerializeResult.IsFailure()) {
        GAME_LOG(Error, TEXT("【存档子系统】自定义对象序列化失败: %s"), *SlotName);
        return TGameStatusResult<void>::Failure(3, SerializeResult.GetError().Message);
    }

    const uint64 SaveGeneration = FSaveGameFileIO::IssueSlotSaveGeneration(SlotName);
    return FSaveGameFileIO::WriteSlotBytes(SlotName, SerializeResult.ExtractValue(), SaveGeneration);
}

/**
 * @brief 从指定槽位同步加载任意派生自 USaveGame 的对象（蓝图专用外观方法）
 * @param SlotName 目标存档槽位名称
 * @param SaveGameClassToLoad 目标加载的 USaveGame 类别
 * @param OutSaveGame 输出加载并反序列化完成的对象引用
 * @return 加载成功返回 true，否则返回 false
 */
bool USaveGameSubsystem::K2_LoadObjectFromSlot(const FString& SlotName, TSubclassOf<USaveGame> SaveGameClassToLoad, USaveGame*& OutSaveGame) {
    auto Result = LoadObjectFromSlot(SlotName, SaveGameClassToLoad);
    if (Result.IsSuccess()) {
        OutSaveGame = Result.GetValue();
        return true;
    }
    OutSaveGame = nullptr;
    return false;
}

/**
 * @brief 从指定槽位同步加载任意派生自 USaveGame 的对象
 * @param SlotName 目标存档槽位名称
 * @param SaveGameClassToLoad 目标加载的 USaveGame 类别
 * @return 成功返回加载对象的强类型指针，失败返回含错误原因的 Failure
 */
TGameResult<USaveGame*, FGameError> USaveGameSubsystem::LoadObjectFromSlot(const FString& SlotName, TSubclassOf<USaveGame> SaveGameClassToLoad) {
    if (SaveGameClassToLoad == nullptr) {
        return TGameResult<USaveGame*, FGameError>::Failure(FGameError(TEXT("SaveGameClassToLoad 为空")));
    }
    if (!IsSlotNameAllowed(SlotName)) {
        return TGameResult<USaveGame*, FGameError>::Failure(FGameError(FString::Printf(TEXT("槽位名称 '%s' 不在白名单中或非法"), *SlotName)));
    }

    auto ReadResult = FSaveGameFileIO::ReadSlotBytes(SlotName);
    if (ReadResult.IsFailure()) {
        return TGameResult<USaveGame*, FGameError>::Failure(ReadResult.GetError());
    }

    USaveGame* LoadedObject = UGameplayStatics::LoadGameFromMemory(ReadResult.ExtractValue());
    if (LoadedObject == nullptr) {
        GAME_LOG(Error, TEXT("【存档子系统】存档反序列化失败或类型不匹配: %s"), *SlotName);
        return TGameResult<USaveGame*, FGameError>::Failure(FGameError(FString::Printf(TEXT("反序列化存档对象失败: %s"), *SlotName)));
    }
    if (!LoadedObject->IsA(SaveGameClassToLoad)) {
        GAME_LOG(Error, TEXT("【存档子系统】存档反序列化失败或类型不匹配: %s"), *SlotName);
        return TGameResult<USaveGame*, FGameError>::Failure(FGameError(FString::Printf(
            TEXT("存档对象类型不匹配 '%s': 期望 %s, 实际为 %s"),
            *SlotName, *SaveGameClassToLoad->GetName(), *LoadedObject->GetClass()->GetName())));
    }

    return TGameResult<USaveGame*, FGameError>::Success(LoadedObject);
}

/**
 * @brief 检查指定槽位的存档文件是否存在
 * @param SlotName 目标存档槽位名称
 * @return 存在任意有效存档文件返回 true，否则返回 false
 */
bool USaveGameSubsystem::DoesSaveSlotExist(const FString& SlotName) const {
    if (!IsSlotNameAllowed(SlotName)) {
        return false;
    }
    return FSaveGameFileIO::DoesSaveSlotExist(SlotName);
}

/**
 * @brief 彻底删除指定槽位的存档物理文件，并使该槽位在途异步加载失效（蓝图专用外观方法）
 * @param SlotName 目标存档槽位名称
 * @return 删除成功返回 true，未找到或删除失败返回 false
 */
bool USaveGameSubsystem::K2_DeleteSaveSlot(const FString& SlotName) {
    return DeleteSaveSlot(SlotName).IsSuccess();
}

/**
 * @brief 彻底删除指定槽位的存档物理文件，并使该槽位在途异步加载失效
 * @param SlotName 目标存档槽位名称
 * @return 成功删除返回 Success，未找到或删除失败返回 Failure
 */
TGameStatusResult<void> USaveGameSubsystem::DeleteSaveSlot(const FString& SlotName) {
    if (!IsSlotNameAllowed(SlotName)) {
        return TGameStatusResult<void>::Failure(1, FString::Printf(TEXT("槽位名称 '%s' 不在白名单中或非法"), *SlotName));
    }
    SegmentRegistry.ClearPreservedSegmentsForSlot(SlotName);
    ++AsyncLoadGenerations.FindOrAdd(SlotName);
    return FSaveGameFileIO::DeleteSaveSlot(SlotName);
}

/**
 * @brief 注册（或覆盖）具名存档段契约
 * @param Segment 待注册的存档段契约
 */
void USaveGameSubsystem::RegisterSegment(const FGameSaveSegment& Segment) {
    SegmentRegistry.RegisterSegment(Segment);
}

/**
 * @brief 注销指定名称的存档段契约
 * @param SegmentName 目标存档段名称
 * @return 成功注销返回 Success，未找到返回 Failure
 */
TGameStatusResult<void> USaveGameSubsystem::UnregisterSegment(FName SegmentName) {
    return SegmentRegistry.UnregisterSegment(SegmentName);
}

/**
 * @brief 查询指定名称的存档段是否已注册
 * @param SegmentName 目标存档段名称
 * @return 已注册返回 true，否则返回 false
 */
bool USaveGameSubsystem::HasSegment(FName SegmentName) const {
    return SegmentRegistry.HasSegment(SegmentName);
}
