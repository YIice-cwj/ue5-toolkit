/**
 * @file USaveGameSubsystem.cpp
 * @brief 通用存档与持久化调度子系统实现
 */

#include "Save/USaveGameSubsystem.h"

#include "Async/FGameThreadTaskRunner.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Logging/GameLogCategories.h"
#include "Save/FSaveGameFileIO.h"
#include "UObject/Package.h"

/**
 * @brief 构造函数：初始化默认分段归档类型为 UGameSaveArchive
 */
USaveGameSubsystem::USaveGameSubsystem()
    : SaveGameClass(UGameSaveArchive::StaticClass()) {
}

/**
 * @brief 子系统初始化生命周期回调：重置异步生命周期取消源
 * @param Collection 子系统依赖集合
 */
void USaveGameSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    LifetimeCancelSource = FCancellationTokenSource();
}

/**
 * @brief 子系统销毁生命周期回调：取消在途异步任务并清空分段注册表、白名单集合与异步加载代次
 */
void USaveGameSubsystem::Deinitialize() {
    LifetimeCancelSource.Cancel();
    SegmentRegistry.Reset();
    AllowedSlotNames.Empty();
    AsyncLoadGenerations.Empty();
    Super::Deinitialize();
}

/**
 * @brief 获取当前上下文的存档子系统指针（支持 UWorld、UGameInstance、UGameInstanceSubsystem 与 AActor 等任意上下文）
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
 * @brief 设置自定义的分段归档类型（继承自 UGameSaveArchive 且不得为抽象类）
 * @param InSaveGameClass 目标归档类
 */
void USaveGameSubsystem::SetSaveGameClass(TSubclassOf<UGameSaveArchive> InSaveGameClass) {
    if (InSaveGameClass == nullptr) {
        return;
    }
    if (InSaveGameClass->HasAnyClassFlags(CLASS_Abstract)) {
        GAME_LOG(Warning, TEXT("【存档子系统】拒绝设置抽象归档类: %s"), *InSaveGameClass->GetName());
        return;
    }
    SaveGameClass = InSaveGameClass;
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
 * @return 成功返回 Success，失败返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<void> USaveGameSubsystem::SaveGameToSlot(const FString& SlotName) {
    if (!IsSlotNameAllowed(SlotName)) {
        GAME_LOG(Warning, TEXT("【存档子系统】槽位名不合法或不在白名单内: %s"), *SlotName);
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::InvalidSlotName,
            FString::Printf(TEXT("槽位名称 '%s' 不在白名单中或非法"), *SlotName)
        );
    }

    TGameStatusResult<TArray<uint8>> SerializeResult = BuildAndSerializeArchive(SlotName);
    if (SerializeResult.IsFailure()) {
        return TGameStatusResult<void>::Failure(SerializeResult.ExtractError());
    }

    ++AsyncLoadGenerations.FindOrAdd(SlotName);
    const uint64 SaveGeneration = FSaveGameFileIO::IssueSlotSaveGeneration(SlotName);
    const TGameStatusResult<void> IoResult = FSaveGameFileIO::WriteSlotBytes(SlotName, SerializeResult.ExtractValue(), SaveGeneration);
    if (IoResult.IsSuccess()) {
        GAME_LOG(Log, TEXT("【存档子系统】同步保存槽位成功: %s"), *SlotName);
    } else {
        GAME_LOG(Error, TEXT("【存档子系统】同步保存槽位失败: %s (%s)"), *SlotName, *IoResult.GetError().Message);
    }
    return IoResult;
}

/**
 * @brief 异步保存全部已注册段至指定槽位（主线程捕获与序列化，后台仅写文件，完成后回主线程广播）
 * @param SlotName 目标存档槽位名称
 * @return 成功派发异步任务返回 true，前置校验或序列化失败返回 false
 */
bool USaveGameSubsystem::SaveGameToSlotAsync(const FString& SlotName) {
    if (!IsSlotNameAllowed(SlotName)) {
        GAME_LOG(Warning, TEXT("【存档子系统】槽位名不合法或不在白名单内: %s"), *SlotName);
        OnSaveComplete.Broadcast(SlotName, false);
        return false;
    }

    TGameStatusResult<TArray<uint8>> SerializeResult = BuildAndSerializeArchive(SlotName);
    if (SerializeResult.IsFailure()) {
        OnSaveComplete.Broadcast(SlotName, false);
        return false;
    }

    ++AsyncLoadGenerations.FindOrAdd(SlotName);
    const uint64 SaveGeneration = FSaveGameFileIO::IssueSlotSaveGeneration(SlotName);
    const FCancellationToken LifetimeToken = LifetimeCancelSource.GetToken();
    TWeakObjectPtr<USaveGameSubsystem> WeakThis(this);
    FGameThreadTaskRunner::Run(
        WeakThis,
        LifetimeToken,
        [WeakThis, LifetimeToken, SlotName, SaveGeneration, Payload = SerializeResult.ExtractValue()]() mutable {
            bool bSkippedStale = false;
            const bool bWriteOk = FSaveGameFileIO::WriteSlotBytes(SlotName, Payload, SaveGeneration, &bSkippedStale).IsSuccess();
            const bool bIoSuccess = bWriteOk && !bSkippedStale;
            FGameThreadTaskRunner::RunOnGameThread(WeakThis, [WeakThis, LifetimeToken, SlotName, bIoSuccess, bSkippedStale]() {
                if (LifetimeToken.IsCancellationRequested()) {
                    return;
                }
                if (bIoSuccess) {
                    GAME_LOG(Log, TEXT("【存档子系统】异步保存槽位成功: %s"), *SlotName);
                } else if (bSkippedStale) {
                    GAME_LOG(Log, TEXT("【存档子系统】过期异步保存已跳过: %s"), *SlotName);
                } else {
                    GAME_LOG(Error, TEXT("【存档子系统】异步保存槽位失败: %s"), *SlotName);
                }
                WeakThis->OnSaveComplete.Broadcast(SlotName, bIoSuccess);
            });
        }
    );

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
 * @return 成功返回 Success，失败返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<void> USaveGameSubsystem::LoadGameFromSlot(const FString& SlotName) {
    if (!IsSlotNameAllowed(SlotName)) {
        GAME_LOG(Warning, TEXT("【存档子系统】槽位名不合法或不在白名单内: %s"), *SlotName);
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::InvalidSlotName,
            FString::Printf(TEXT("槽位名称 '%s' 不在白名单中或非法"), *SlotName)
        );
    }

    TGameStatusResult<TArray<uint8>> ReadResult = FSaveGameFileIO::ReadSlotBytes(SlotName);
    if (ReadResult.IsFailure()) {
        GAME_LOG(Warning, TEXT("【存档子系统】同步装载槽位失败: %s (%s)"), *SlotName, *ReadResult.GetError().Message);
        return TGameStatusResult<void>::Failure(ReadResult.ExtractError());
    }

    TGameStatusResult<UGameSaveArchive*> ValidateResult = DeserializeAndValidateArchive(SlotName, ReadResult.GetValue());
    if (ValidateResult.IsFailure()) {
        return TGameStatusResult<void>::Failure(ValidateResult.ExtractError());
    }

    const TGameStatusResult<void> ApplyResult = ApplyArchiveToSegments(SlotName, *ValidateResult.GetValue());
    if (ApplyResult.IsFailure()) {
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
    const FCancellationToken LifetimeToken = LifetimeCancelSource.GetToken();
    TWeakObjectPtr<USaveGameSubsystem> WeakThis(this);
    FGameThreadTaskRunner::Run(WeakThis, LifetimeToken, [WeakThis, LifetimeToken, SlotName, LoadGeneration]() {
        TGameStatusResult<TArray<uint8>> ReadResult = FSaveGameFileIO::ReadSlotBytes(SlotName);
        FGameThreadTaskRunner::RunOnGameThread(
            WeakThis,
            [WeakThis, LifetimeToken, SlotName, LoadGeneration, ReadResult = MoveTemp(ReadResult)]() mutable {
                if (LifetimeToken.IsCancellationRequested()) {
                    return;
                }
                WeakThis->CompleteAsyncLoad(SlotName, LoadGeneration, MoveTemp(ReadResult));
            }
        );
    });

    return true;
}

/**
 * @brief 在内存中反序列化并校验字节流是否匹配当前归档类型、归档版本及全部已注册段版本（零磁盘 I/O 与零业务状态修改）
 * @param SlotName 目标存档槽位名称（用于诊断日志）
 * @param Bytes 待校验的完整 USaveGame 字节流
 * @return 校验通过返回反序列化后的归档对象指针，失败返回含具体 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<UGameSaveArchive*> USaveGameSubsystem::DeserializeAndValidateArchive(
    const FString& SlotName,
    const TArray<uint8>& Bytes
) const {
    if (!FSaveGameFileIO::HasValidSaveGameHeader(Bytes, SlotName)) {
        return TGameStatusResult<UGameSaveArchive*>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::CorruptedSaveFile,
            FString::Printf(TEXT("存档头校验失败: %s"), *SlotName)
        );
    }

    USaveGame* LoadedObject = UGameplayStatics::LoadGameFromMemory(Bytes);
    if (LoadedObject == nullptr) {
        GAME_LOG(Error, TEXT("【存档子系统】存档反序列化失败: %s"), *SlotName);
        return TGameStatusResult<UGameSaveArchive*>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::SaveDeserializeFailed,
            FString::Printf(TEXT("反序列化存档对象失败: %s"), *SlotName)
        );
    }
    if (!LoadedObject->IsA(SaveGameClass)) {
        GAME_LOG(Error, TEXT("【存档子系统】存档对象类型不匹配 '%s': 期望 %s, 实际为 %s"),
            *SlotName, *SaveGameClass->GetName(), *LoadedObject->GetClass()->GetName());
        return TGameStatusResult<UGameSaveArchive*>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::SaveTypeMismatch,
            FString::Printf(
                TEXT("存档对象类型不匹配 '%s': 期望 %s, 实际为 %s"),
                *SlotName,
                *SaveGameClass->GetName(),
                *LoadedObject->GetClass()->GetName()
            )
        );
    }

    UGameSaveArchive* Archive = CastChecked<UGameSaveArchive>(LoadedObject);
    if (Archive->Metadata.ArchiveVersion <= 0) {
        GAME_LOG(Error, TEXT("【存档子系统】存档版本 %d 非法（须为正整数），拒绝加载: %s"),
            Archive->Metadata.ArchiveVersion, *SlotName);
        return TGameStatusResult<UGameSaveArchive*>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::ArchiveVersionMismatch,
            FString::Printf(
                TEXT("归档版本 %d 非法（须为正整数）"),
                Archive->Metadata.ArchiveVersion
            )
        );
    }
    if (Archive->Metadata.ArchiveVersion > UGameSaveArchive::CurrentArchiveVersion) {
        GAME_LOG(Error, TEXT("【存档子系统】存档版本 %d 高于当前支持版本 %d，拒绝加载: %s"),
            Archive->Metadata.ArchiveVersion, UGameSaveArchive::CurrentArchiveVersion, *SlotName);
        return TGameStatusResult<UGameSaveArchive*>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::ArchiveVersionMismatch,
            FString::Printf(
                TEXT("归档版本 %d 高于当前支持的版本 %d"),
                Archive->Metadata.ArchiveVersion,
                UGameSaveArchive::CurrentArchiveVersion
            )
        );
    }

    GAME_TRY(SegmentRegistry.ValidateArchiveCompatibility(*Archive));
    return TGameStatusResult<UGameSaveArchive*>::Success(Archive);
}

/**
 * @brief 将已校验通过的内存归档对象逐段应用至已注册段（消除落盘后二次读盘）
 * @param SlotName 目标存档槽位名称
 * @param Archive 已通过反序列化与版本校验的归档对象引用
 * @return 应用成功返回 Success，失败返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<void> USaveGameSubsystem::ApplyArchiveToSegments(
    const FString& SlotName,
    const UGameSaveArchive& Archive
) {
    ++AsyncLoadGenerations.FindOrAdd(SlotName);
    const TGameStatusResult<void> ApplyResult = SegmentRegistry.ApplyArchive(SlotName, Archive);
    if (ApplyResult.IsFailure()) {
        GAME_LOG(Warning, TEXT("【存档子系统】同步装载槽位段应用失败: %s (%s)"), *SlotName, *ApplyResult.GetError().Message);
    }
    return ApplyResult;
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
 * @return 成功返回 Success，失败返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<void> USaveGameSubsystem::SaveObjectToSlot(const FString& SlotName, USaveGame* SaveGameObject) {
    if (SaveGameObject == nullptr) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::InvalidSaveObject,
            TEXT("SaveGameObject 为空")
        );
    }
    if (!IsSlotNameAllowed(SlotName)) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::InvalidSlotName,
            FString::Printf(TEXT("槽位名称 '%s' 不在白名单中或非法"), *SlotName)
        );
    }

    TGameStatusResult<TArray<uint8>> SerializeResult = FSaveGameFileIO::SerializeObject(SaveGameObject);
    if (SerializeResult.IsFailure()) {
        GAME_LOG(Error, TEXT("【存档子系统】自定义对象序列化失败: %s"), *SlotName);
        return TGameStatusResult<void>::Failure(SerializeResult.ExtractError());
    }

    ++AsyncLoadGenerations.FindOrAdd(SlotName);
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
    const TGameStatusResult<USaveGame*> Result = LoadObjectFromSlot(SlotName, SaveGameClassToLoad);
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
 * @return 成功返回加载对象的强类型指针，失败返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<USaveGame*> USaveGameSubsystem::LoadObjectFromSlot(const FString& SlotName, TSubclassOf<USaveGame> SaveGameClassToLoad) {
    if (SaveGameClassToLoad == nullptr || SaveGameClassToLoad->HasAnyClassFlags(CLASS_Abstract)) {
        return TGameStatusResult<USaveGame*>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::InvalidSaveClass,
            TEXT("SaveGameClassToLoad 为空或为抽象类")
        );
    }
    if (!IsSlotNameAllowed(SlotName)) {
        return TGameStatusResult<USaveGame*>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::InvalidSlotName,
            FString::Printf(TEXT("槽位名称 '%s' 不在白名单中或非法"), *SlotName)
        );
    }

    TGameStatusResult<TArray<uint8>> ReadResult = FSaveGameFileIO::ReadSlotBytes(SlotName);
    if (ReadResult.IsFailure()) {
        return TGameStatusResult<USaveGame*>::Failure(ReadResult.ExtractError());
    }

    USaveGame* LoadedObject = UGameplayStatics::LoadGameFromMemory(ReadResult.ExtractValue());
    if (LoadedObject == nullptr) {
        GAME_LOG(Error, TEXT("【存档子系统】存档反序列化失败: %s"), *SlotName);
        return TGameStatusResult<USaveGame*>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::SaveDeserializeFailed,
            FString::Printf(TEXT("反序列化存档对象失败: %s"), *SlotName)
        );
    }
    if (!LoadedObject->IsA(SaveGameClassToLoad)) {
        GAME_LOG(Error, TEXT("【存档子系统】存档对象类型不匹配 '%s': 期望 %s, 实际为 %s"),
            *SlotName, *SaveGameClassToLoad->GetName(), *LoadedObject->GetClass()->GetName());
        return TGameStatusResult<USaveGame*>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::SaveTypeMismatch,
            FString::Printf(
                TEXT("存档对象类型不匹配 '%s': 期望 %s, 实际为 %s"),
                *SlotName,
                *SaveGameClassToLoad->GetName(),
                *LoadedObject->GetClass()->GetName()
            )
        );
    }

    return TGameStatusResult<USaveGame*>::Success(LoadedObject);
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
 * @return 成功删除返回 Success，未找到或删除失败返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<void> USaveGameSubsystem::DeleteSaveSlot(const FString& SlotName) {
    if (!IsSlotNameAllowed(SlotName)) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::InvalidSlotName,
            FString::Printf(TEXT("槽位名称 '%s' 不在白名单中或非法"), *SlotName)
        );
    }
    SegmentRegistry.ClearPreservedSegmentsForSlot(SlotName);
    ++AsyncLoadGenerations.FindOrAdd(SlotName);
    return FSaveGameFileIO::DeleteSaveSlot(SlotName);
}

/**
 * @brief 注册（或覆盖）具名存档段契约
 * @param Segment 待注册的存档段契约
 * @return 注册成功返回 Success，参数非法返回 InvalidSegment 失败
 */
TGameStatusResult<void> USaveGameSubsystem::RegisterSegment(const FGameSaveSegment& Segment) {
    return SegmentRegistry.RegisterSegment(Segment);
}

/**
 * @brief 注销指定名称的存档段契约
 * @param SegmentName 目标存档段名称
 * @return 成功注销返回 Success，未找到返回 SegmentNotFound 失败
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

/**
 * @brief 主线程组装全部已注册段并序列化为二进制字节流（同步与异步保存共享）
 * @param SlotName 目标存档槽位名称
 * @return 成功返回二进制字节流，失败返回含 Game::DataErrorCodes 的 Failure
 */
TGameStatusResult<TArray<uint8>> USaveGameSubsystem::BuildAndSerializeArchive(const FString& SlotName) const {
    UGameSaveArchive* Archive = NewObject<UGameSaveArchive>(GetTransientPackage(), SaveGameClass);
    GAME_TRY(SegmentRegistry.BuildArchive(SlotName, *Archive));

    TGameStatusResult<TArray<uint8>> SerializeResult = FSaveGameFileIO::SerializeObject(Archive);
    if (SerializeResult.IsFailure()) {
        GAME_LOG(Error, TEXT("【存档子系统】内存序列化失败: %s (%s)"), *SlotName, *SerializeResult.GetError().Message);
    }
    return SerializeResult;
}

/**
 * @brief 主线程结算异步加载结果（校验代次最新性、反序列化并应用内存段、输出日志与广播事件）
 * @param SlotName 目标存档槽位名称
 * @param LoadGeneration 发起时签发的异步加载代次
 * @param ReadResult 后台读取结果
 */
void USaveGameSubsystem::CompleteAsyncLoad(
    const FString& SlotName,
    uint64 LoadGeneration,
    TGameStatusResult<TArray<uint8>> ReadResult
) {
    const uint64* LatestGeneration = AsyncLoadGenerations.Find(SlotName);
    if (LatestGeneration == nullptr || *LatestGeneration != LoadGeneration) {
        GAME_LOG(Log, TEXT("【存档子系统】过期异步加载已取消: %s"), *SlotName);
        OnLoadComplete.Broadcast(SlotName, false);
        return;
    }

    if (ReadResult.IsFailure()) {
        GAME_LOG(Error, TEXT("【存档子系统】异步装载槽位读盘失败: %s (%s)"), *SlotName, *ReadResult.GetError().Message);
        OnLoadComplete.Broadcast(SlotName, false);
        return;
    }

    TGameStatusResult<UGameSaveArchive*> ValidateResult = DeserializeAndValidateArchive(SlotName, ReadResult.GetValue());
    if (ValidateResult.IsFailure()) {
        GAME_LOG(Error, TEXT("【存档子系统】异步装载槽位校验失败: %s (%s)"), *SlotName, *ValidateResult.GetError().Message);
        OnLoadComplete.Broadcast(SlotName, false);
        return;
    }

    const TGameStatusResult<void> ApplyResult = ApplyArchiveToSegments(SlotName, *ValidateResult.GetValue());
    if (ApplyResult.IsFailure()) {
        GAME_LOG(Error, TEXT("【存档子系统】异步装载槽位应用失败: %s (%s)"), *SlotName, *ApplyResult.GetError().Message);
        OnLoadComplete.Broadcast(SlotName, false);
        return;
    }

    GAME_LOG(Log, TEXT("【存档子系统】异步成功装载槽位: %s"), *SlotName);
    OnLoadComplete.Broadcast(SlotName, true);
}
