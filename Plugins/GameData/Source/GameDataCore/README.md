# GameDataCore

> **一句话**：`GameData` 插件内的通用数据模块，承载生成式静态数据仓库、不可变快照与分段存档基础设施。
> **版本** v2.0.0 · **引擎** UE 5.1~5.8 · **层级** Level 2 · **直接依赖** GameCore, GameLogging, Json

## 30 秒速览

- **解决什么问题**：把与业务域无关的静态数据装载、二进制编解码、快照发布和分段存档收敛到一个模块，供 `GameData` 及业务层复用。
- **核心能力**：
  - `FStaticDataRepository` 类型擦除行仓库与 `FStaticDataBinCodec` 单缓冲 `.bin` 编解码；
  - `TStaticDataSnapshotStore<TSnapshot>` 只读快照发布与无锁读取；
  - `USaveGameSubsystem` 分段存档、版本迁移、槽位白名单与原子文件 I/O；
  - `FDirectoryChangeWatcher` 目录指纹监听与 `FDataTableLoaderUtils` JSON 装载辅助。
- **什么时候用**：需要自建静态数据仓库 / 快照，或为业务接入通用分段存档。
- **什么时候不用**：不随 `GameData` 单独部署，也不承载 Item / Shop / PlayerProfile 等业务字段；纯业务存档请直接注册 `FGameSaveSegment`。

## 快速开始

业务方只需注册一个 `FGameSaveSegment`，无需继承或修改存档子系统：

```cpp
#include "Save/USaveGameSubsystem.h"

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

if (const TGameStatusResult<void> RegRes = SaveSubsystem->RegisterSegment(Segment); RegRes.IsFailure()) {
    GAME_LOG(Error, TEXT("注册段失败: %s"), *RegRes.GetError().Message);
}
if (const TGameStatusResult<void> SaveRes = SaveSubsystem->SaveGameToSlot(TEXT("Slot_1")); SaveRes.IsFailure()) {
    GAME_LOG(Error, TEXT("同步保存失败: %s"), *SaveRes.GetError().Message);
}
```

## 核心能力

### 静态数据仓库与编解码

- `FStaticDataRepository` 为纯 C++ 可独立实例化的类型擦除容器，通过 `Serialize{Struct}` 逐行调度，免反射、与数据表解耦。
- `FStaticDataBinCodec` 提供单缓冲零拷贝 `.bin` 编解码、Magic / 版本校验与负载 CRC32 防破坏校验。
- 摘要使用长度前缀抗碰撞的 UTF-8 SHA-1；错误枚举与表结构定义在 `FStaticDataRepositoryTypes.h`。
- 仓库按结构名 / 行键排序生成内容摘要，可用于冷启动基准与双通道对拍。

### 不可变快照发布

- `TStaticDataSnapshotStore<TSnapshot>` 用 `FRWLock` 保护发布、以无锁路径读取版本。
- 每次提交生成全新快照，读者持有期间内容稳定，不受后续提交影响。
- 代次化热更闸门阻止重入发布；热更与首次加载共用同一条提交流程。

### 通用分段存档

- `FGameSaveSegmentRegistry` 管理段注册、只读版本预检、版本迁移与未知段保留。
- `GameSaveArchive` 保存槽位元数据与具名分段数据映射；`USaveGameSubsystem` 提供同步 / 异步存读、内存预校验与槽位白名单。
- `FSaveGameFileIO` 负责槽位级锁、防断电原子写与高水位代次防覆写。
- `SaveObjectToSlot` / `LoadObjectFromSlot` 是任意 `USaveGame` 的存取逃生口。

### 装载与热更辅助

- `FDataTableLoaderUtils` 以 `MoveTemp` 装配方式辅助 DataTable JSON 反序列化。
- `FDirectoryChangeWatcher` 支持递归 / 非递归指纹采样，把变更事件统一派发到主线程任务执行器。
- 目录监听只负责发现变化，具体重载流程由宿主模块决定。

### 错误码与基准工具

- 静态数据与持久化错误集中在 `Game::DataErrorCodes`，接口统一返回 `TGameStatusResult`。
- `GameDataBenchmarkUtils` 提供共享的微基准采样与统计工具，供静态数据与存档性能用例复用。

## 架构与依赖

| 项目 | 内容 |
| :--- | :--- |
| 模块名 | `GameDataCore`（`GAMEDATACORE_API`） |
| 归属 | `Plugins/GameData/Source/GameDataCore`，随 `GameData` 安装 |
| 公有依赖 | Core, CoreUObject, Engine, Json, JsonUtilities, GameLogging, GameCore |
| 私有依赖 | 无；零网络依赖，不依赖 `GamePool` |
| 业务字段 | 无；不依赖 `GameData` 生成模块 |

- 静态数据入口：`FStaticDataRepository` → `TStaticDataSnapshotStore` → 业务生成的 Query / Interface。
- 存档入口：`FGameSaveSegment` → `FSaveGameSegmentRegistry` → `GameSaveArchive` → `FSaveGameFileIO`。
- 示例接入见 `Plugins/GameDataSamples` 的 `UGamePlayerProfileSaveAdapter`。

## 性能与边界

- **线程**：快照读取为无锁路径；目录监听回调统一在 `FGameThreadTaskRunner` 主线程派发。
- **快照生命周期**：读者持有 `TSharedPtr` 期间内容稳定，提交只切换新快照。
- **存档版本**：`SegmentVersion <= 0` 注册即拒绝；落盘段版本高于注册版本时整档拒绝；未知段保留回写。
- **文件安全**：写入走 `.sav.tmp` 原子替换，槽位级 I/O 锁与内存元数据锁分离。
- **模块边界**：零网络依赖，不包含任何业务域字段，也不反向依赖 `GameData`。

<details>
<summary>展开性能基准（历史 Development 构建参考值）</summary>

下表为自动化测试中的中位耗时预算上限，不是仓库固化的实测值；实际数值随机器与构建配置变化。

| 基准用例 | 场景 | 预算 |
| :--- | :--- | :--- |
| `Game.Data.Perf.StaticData.BinCodec` | 编码 / 解码 1000 行 | < 1 ms / < 500 μs |
| `Game.Data.Perf.StaticData.ContentDigest` | 内容摘要 | < 500 μs |
| `Game.Data.Perf.StaticData.JsonLoad` | 500 行 JSON 加载 | < 25 ms |
| `Game.Data.Perf.StaticData.RepositoryRoundTrip` | 内存 Add+Extract / 文件 Export+Import（1000 行） | < 2 ms / < 5 ms |
| `Game.Data.Perf.StaticData.SnapshotStore` | `Acquire` / 无锁 `GetVersion` | < 500 ns / < 100 ns |
| `Game.Data.Perf.StaticData.DirectoryWatcherFingerprint` | 32 文件指纹扫描 | < 5 ms |
| `Game.Data.Perf.SaveGame.SegmentRegistry` | 16 段构建 / 恢复 | < 150 μs / < 100 μs |
| `Game.Data.Perf.SaveGame.AtomicFileIO` | 4 KB 写 / 读 | < 10 ms / < 1 ms |
| `Game.Data.Perf.SaveGame.AtomicFileIO` | 64 KB 写 / 读 | < 20 ms / < 2 ms |
| `Game.Data.Perf.SaveGame.SubsystemRoundTrip` | 同步存读往返 | < 15 ms |

来源：`Source/GameDataCore/Private/Tests/StaticDataPerformanceTest.cpp`、`SaveGamePerformanceTest.cpp`。

</details>

## 文档导航

| 文档 | 内容 |
| :--- | :--- |
| [GameData 插件说明](../../README.md) | 双模块分工、生成管线与云存档门面 |
| [静态数据仓库 API](../../Docs/API/StaticDataRepository.md) | `FStaticDataRepository` / `FStaticDataBinCodec` / `TStaticDataSnapshotStore` |
| [存档子系统 API](../../Docs/API/SaveGameSubsystem.md) | `USaveGameSubsystem` 接口与错误码 |
| [持久化架构](../../Docs/Architecture/PersistenceArchitecture.md) | 分段存档、原子写与迁移 |
| [存档接入指南](../../Docs/Guides/SaveGameGuide.md) | 业务接入方式 |
| [GameDataSamples 示例](../../../GameDataSamples/README.md) | `UGamePlayerProfileSaveAdapter` 示例 |
