# GameDataCore 模块

**GameDataCore** 是 `GameData` 插件内的通用数据模块（`GAMEDATACORE_API`），承载与具体业务域无关的静态数据装载与持久化基础设施；随 `GameData` 插件一起安装，不单独部署。

---

## 职责边界

| 能力 | 核心文件 |
| :--- | :--- |
| 类型擦除的二进制仓库（生成式行读写器、免反射容器） | `Public/StaticData/FStaticDataRepository.h` |
| 二进制格式编解码器（Magic / 版本 / CRC32 / 跨平台 UTF-8 摘要） | `Public/StaticData/FStaticDataBinCodec.h` |
| DataTable JSON 装载辅助（零拷贝 MoveTemp 装配） | `Public/Utils/DataTableLoaderUtils.h` |
| 通用分段存档类型与元数据（段版本 / 字节载荷 / 槽位元信息） | `Public/Save/GameSaveTypes.h` |
| 通用分段归档对象（槽位元数据 + 具名分段数据映射） | `Public/Save/GameSaveArchive.h` |
| 存档分段注册表（委托托管、版本平滑迁移、未知段跨版本保留） | `Public/Save/FSaveGameSegmentRegistry.h` |
| 存档文件底层 I/O（防断电原子写、高水位代次防并发覆写、容灾恢复） | `Public/Save/FSaveGameFileIO.h` |
| 通用存档子系统门面（段注册、槽位白名单、异步最新优先调度） | `Public/Save/USaveGameSubsystem.h` |

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
