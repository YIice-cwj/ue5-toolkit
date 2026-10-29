# GameData 存档接入指南

`USaveGameSubsystem` 不保存任何业务字段；业务方通过**存档段**接入。

---

## 1. 注册一个存档段

```cpp
#include "Save/USaveGameSubsystem.h"

void UMyGameInstance::Init() {
    Super::Init();

    USaveGameSubsystem* SaveSubsystem = GetSubsystem<USaveGameSubsystem>();
    SaveSubsystem->RegisterSegment(BuildProfileSegment());
    SaveSubsystem->SetAllowedSlotNames({TEXT("Slot_1"), TEXT("Slot_2"), TEXT("Slot_3")});
}

FGameSaveSegment UMyGameInstance::BuildProfileSegment() {
    FGameSaveSegment Segment;
    Segment.SegmentName = TEXT("Player.Profile");
    Segment.SegmentVersion = 2;

    Segment.Save = [this](FArchive& Ar) {
        FString PlayerIdCopy = PlayerProfile.PlayerId;
        int32 Level = PlayerProfile.Level;
        int64 Gold = PlayerProfile.Gold;
        Ar << PlayerIdCopy;
        Ar << Level;
        Ar << Gold;
    };

    Segment.Load = [this](FArchive& Ar, int32 StoredVersion) {
        FString LoadedId;
        int32 Level = 0;
        int64 Gold = 0;
        Ar << LoadedId;
        Ar << Level;
        Ar << Gold;

        if (StoredVersion < 2) {
            Gold = MigrateGoldFromV1(Gold);
        }
        PlayerProfile = FMyProfile{LoadedId, Level, Gold};
        return true;
    };

    return Segment;
}
```

要点：

- `Save` 在主线程被调用，只做内存序列化，不要执行磁盘 I/O。
- `Save` 若检测到依赖的外部数据源或弱引用失效，应主动调用 `Ar.SetError()` 终止归档，防止写入空白数据导致原有存档被覆盖清空。
- `Load` 收到的 `StoredVersion` 是落盘版本；版本高于当前段版本时子系统会拒绝该段。
- 段名稳定不变；新增段不会破坏旧档。

---

## 2. 保存与加载

```cpp
SaveSubsystem->SaveGameToSlotAsync(TEXT("Slot_1"));
SaveSubsystem->LoadGameFromSlot(TEXT("Slot_1"));
```

异步路径广播：

```cpp
SaveSubsystem->OnSaveComplete.AddDynamic(this, &UMyHud::HandleSaveComplete);
SaveSubsystem->OnLoadComplete.AddDynamic(this, &UMyHud::HandleLoadComplete);
```

同步路径通过返回值判断；失败原因见日志。

---

## 3. 槽位管理

```cpp
const bool bExists = SaveSubsystem->DoesSaveSlotExist(TEXT("Slot_1"));
SaveSubsystem->DeleteSaveSlot(TEXT("Slot_1"));
```

槽位名默认拒绝空名、超长名、`/`、`\`、`.`；白名单为空表示不额外限制。
`DeleteSaveSlot` 在物理删除正式档与清理临时档的同时，会使槽位保存代次失效（置为 `MAX_uint64`），彻底阻止在途并发的后台异步写任务复活已删除槽位。

---

## 4. 自定义存档类

需要额外 UPROPERTY 字段时：

```cpp
UCLASS()
class UMySaveArchive : public UGameSaveArchive {
    GENERATED_BODY()

public:
    UPROPERTY()
    FString BuildVersion;
};

SaveSubsystem->SetSaveGameClass(UMySaveArchive::StaticClass());
```

段数据仍存于 `Segments`，自定义字段随存档对象一起复制。

---

## 5. 非分段对象逃生口

```cpp
USaveGameSubsystem* SaveSubsystem = GetSubsystem<USaveGameSubsystem>();
SaveSubsystem->SaveObjectToSlot(TEXT("Settings"), SettingsObject);

USaveGame* Loaded = nullptr;
SaveSubsystem->LoadObjectFromSlot(TEXT("Settings"), UMySettings::StaticClass(), Loaded);
```

该路径同样带 `.sav.tmp` 原子替换与损坏档前置校验。

---

## 6. 常见错误

| 现象 | 原因 | 处理 |
| :--- | :--- | :--- |
| 保存返回 false | 段未注册 / 槽位名非法 / 磁盘写失败 | 检查 `HasSegment`、`IsSlotNameAllowed` 与日志 |
| 加载返回 false | 档损坏 / 段版本高于当前 / 文件缺失 | 查看 Warning；确认段版本只增不减 |
| 读取后旧段丢失 | 注册段名与保存时不一致 | 段名视为持久化契约，禁止随版本重命名 |
