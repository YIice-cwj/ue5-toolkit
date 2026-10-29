# GameData 快速上手指南

本指南带您在业务模块中快速接入 `GameData`：查询生成式静态配表，并通过通用存档段保存业务进度。

---

## 1. 添加模块依赖

在业务模块的 `*.Build.cs` 中添加：

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core",
    "CoreUObject",
    "Engine",
    "GameData",
});
```

`GameData` 已公开传递依赖 `GameDataCore`、`GameLogging`、`GameCore`，因此静态数据子系统与存档子系统都无需重复声明。

> 生成代码（`Generated/**`）写入 `GameData` 模块；安装到目标工程后，表名与目录随您的 Excel 结构变化。

---

## 2. 静态数据查询实战

```cpp
#include "Generated/Data/StaticData/UStaticDataSubsystem.h"
#include "Generated/Data/StaticData/Item/ItemQuery.h"

void AMyGameMode::CheckPlayerItem(FName ItemId)
{
    UStaticDataSubsystem* StaticData = GetGameInstance()->GetSubsystem<UStaticDataSubsystem>();
    if (!StaticData) return;

    // 1. 在本次逻辑栈内持有当前不可变快照
    const TSharedPtr<const FStaticDataSnapshot> Snapshot = StaticData->GetSnapshot();

    // 2. 主键查询：返回持快照所有权的行共享指针，可跨版本安全持有
    const TSharedPtr<const FItemRow> ItemRow = FItemQuery::FindByKey(Snapshot, ItemId);
    if (!ItemRow) return;

    GAME_LOG(Log, TEXT("Item %s: Name=%s, GoldCost=%.1f"),
        *ItemId.ToString(), *ItemRow->ItemName, ItemRow->GoldCost);
}
```

- 键类型由生成配置 `row_key_mode` 决定：`fname`（默认）用 `FName`，`enum` 用生成的 `E{Stem}Key`。
- 需要跨表聚合时改用 `FItemStaticDataInterface::FindCompositeData(Snapshot, Key)`。
- 订阅数据版本变化可用 `StaticData->GetDataVersionObserver()`，或绑定 `OnStaticDataReloaded`。

---

## 3. 玩家存档读写实战

存档按「段」注册，子系统不感知任何业务字段。每个业务系统注册自己的段名、段版本与序列化委托：

```cpp
#include "Save/USaveGameSubsystem.h"

FGameSaveSegment UMyProgressSubsystem::BuildProgressSegment()
{
    FGameSaveSegment Segment;
    Segment.SegmentName = TEXT("Progress");
    Segment.SegmentVersion = 1;
    Segment.Save = [this](FArchive& Ar)
    {
        Ar << PlayerLevel;
        Ar << GoldCoins;
    };
    Segment.Load = [this](FArchive& Ar, int32 StoredVersion)
    {
        Ar << PlayerLevel;
        Ar << GoldCoins;
        return true;
    };
    return Segment;
}

void UMyProgressSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    if (USaveGameSubsystem* SaveSys = GetGameInstance()->GetSubsystem<USaveGameSubsystem>())
    {
        SaveSys->RegisterSegment(BuildProgressSegment());
    }
}

void UMyProgressSubsystem::SaveProgress()
{
    USaveGameSubsystem* SaveSys = GetGameInstance()->GetSubsystem<USaveGameSubsystem>();
    if (!SaveSys) return;

    PlayerLevel += 1;
    GoldCoins += 500;

    const bool bSuccess = SaveSys->SaveGameToSlot(TEXT("SaveSlot_01"));
    GAME_LOG(Log, TEXT("Save game result: %s"), bSuccess ? TEXT("Success") : TEXT("Failed"));
}
```

- 异步版本：`SaveGameToSlotAsync` / `LoadGameFromSlotAsync`，完成后触发 `OnSaveComplete` / `OnLoadComplete`。
- 段版本迁移：`Load` 委托收到落盘时的 `StoredVersion`，由业务方自行升级旧格式；落盘版本高于注册版本时整档拒绝。
- 槽位名默认拒绝空名、超长名与路径分隔符；可用 `SetAllowedSlotNames` 限定白名单。
- 需要完全自定义的存档类时，可用 `SaveObjectToSlot` / `LoadObjectFromSlot` 逃生口。

源码参考：[USaveGameSubsystem.h](../../Source/GameDataCore/Public/Persistence/USaveGameSubsystem.h)、[GameSaveArchive.h](../../Source/GameDataCore/Public/Persistence/GameSaveArchive.h)。

---

## 4. 下一步

- [静态数据查询指南](./StaticDataQueryGuide.md)
- [存档指南](./SaveGameGuide.md)
- [架构概览](../Architecture/Overview.md)
