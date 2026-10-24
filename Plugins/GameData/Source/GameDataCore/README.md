# GameDataCore 模块

**GameDataCore** 是 `GameData` 插件内的通用数据模块（`GAMEDATACORE_API`），承载与具体业务域无关的静态数据装载与持久化基础设施；随 `GameData` 插件一起安装，不单独部署。

---

## 职责边界

| 能力 | 核心文件 |
| :--- | :--- |
| 类型擦除的二进制仓库（生成式行读写器、CRC32 校验） | `Public/Data/StaticData/FStaticDataRepository.h` |
| DataTable JSON 装载辅助 | `Public/Utils/DataTableLoaderUtils.h` |
| 通用分段存档对象（段名 / 段版本 / 字节载荷） | `Public/Persistence/GameSaveArchive.h` |
| 通用存档子系统（段注册、版本迁移、槽位白名单、异步落盘） | `Public/Persistence/USaveGameSubsystem.h` |

`GameDataCore` 不包含任何 Item / Shop / PlayerProfile 等业务字段，也不依赖 `GameData` 生成模块。

---

## 依赖

- 公有：`Core`, `CoreUObject`, `Engine`, `Json`, `JsonUtilities`, `GameLogging`
- 私有：`GameCore`

---

## 通用分段存档接入

业务方只需注册一个 `FGameSaveSegment`，无需继承或修改存档子系统：

```cpp
FGameSaveSegment Segment;
Segment.SegmentName = TEXT("Player.Profile");
Segment.SegmentVersion = 1;
Segment.Save = [this](FArchive& Ar) {
    FString PlayerIdCopy = PlayerId;
    Ar << PlayerIdCopy;
    Ar << Level;
};
Segment.Load = [this](FArchive& Ar, int32 StoredVersion) {
    FString LoadedId;
    Ar << LoadedId;
    Ar << Level;
    PlayerId = LoadedId;
    return true;
};

SaveSubsystem->RegisterSegment(Segment);
SaveSubsystem->SaveGameToSlot(TEXT("Slot_1"));
```

- `SegmentVersion` 由段自身维护，`Load` 收到的是落盘版本，可在此完成旧版迁移。
- 槽位名默认拒绝空名、超长名与 `/` `\` `.`；可用 `SetAllowedSlotNames` 进一步限制。
- 存档对象类型可通过 `SetSaveGameClass` 替换为 `UGameSaveArchive` 派生类。
- 任意 `USaveGame` 可通过 `SaveObjectToSlot` / `LoadObjectFromSlot` 逃生口存取。

示例实现见 `Plugins/GameDataSamples` 的 `UGamePlayerProfileSaveAdapter`。
