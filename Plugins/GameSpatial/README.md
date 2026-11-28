# GameSpatial

> **一句话**：不依赖物理引擎的 2D / 3D 空间哈希网格、世界级 Actor 分层索引与视锥 Tick 节流基础设施。
> **版本** v2.0.0 · **引擎** UE 5.1~5.8 · **层级** Level 1 · **直接依赖** GameCore（公开）· GameLogging（私有）

## 30 秒速览

- **解决什么问题**：在没有物理场景参与的情况下，对逻辑对象和 Actor 做可预测的范围 / 邻近检索，并把距离与视锥重要度映射为 Tick 频率。
- **核心能力**
  - `TSpatialHashGrid2D<T>` / `TSpatialHashGrid3D<T>`：均匀哈希网格，O(1) 插入、更新与移除。
  - 半径、AABB、扇形 / 圆柱 / 锥体范围查询，以及最近邻 / K 近邻检索。
  - `UGameSpatialSubsystem`：按 `GameplayTag` 分层索引世界 Actor，蓝图与 C++ 共用。
  - `FSpatialThrottleDriver`：按距离档位、视锥包含度与重要度驱动 `FThrottledTickScheduler`。
- **什么时候用**：技能范围判定、AOI / 邻近目标筛选、掉落物与投射物检索、AI 与表现对象按距离降频 Tick。
- **什么时候不用**：需要真实碰撞响应、射线检测或寻路时交给 Chaos / Physics / Navigation；跨服务器空间分区与空间数据持久化也不在本插件职责内。

## 快速开始

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core",
    "CoreUObject",
    "Engine",
    "GameplayTags",
    "GameCore",
    "GameSpatial",
});
```

```cpp
#include "Grid/TSpatialHashGrid3D.h"

TSpatialHashGrid3D<int32> Grid(500.0f);
Grid.Reserve(1024);
Grid.Insert(1001, FVector::ZeroVector);

Grid.QueryConeMatching(
    FVector::ZeroVector,
    FVector::ForwardVector,
    1200.0f,
    30.0f,
    [](int32 TargetId, const FVector& Location) {
        // 命中目标处理
    });
```

Actor 索引侧可通过 `GetWorld()->GetSubsystem<UGameSpatialSubsystem>()` 调用 `RegisterActor` / `ForEachActorInRadius` 等接口；也可直接包含聚合门面 `#include "GameSpatial.h"`。

## 核心能力

### 2D / 3D 均匀空间哈希网格

- `TSpatialHashGrid2D<TElement>` 与 `TSpatialHashGrid3D<TElement>` 是唯一公开网格门面，底层容器与查询实现收敛在 `Public/Grid/Internal/`（`Game::Spatial::Detail` 命名空间）。
- 单元格键复用 `Game::Hash::Hash64To32` 雪崩散列；`Insert` 单次哈希探查完成查重与写入，跨格移动与移除使用 O(1) Swap-Pop 并自动回收空桶。
- UE5 LWC 双精度映射：底层以 `double InvCellSize` 与 `FloorToClampedCellCoord` 计算格网索引；3D 采用 21 位有符号偏置压缩，默认 `500cm` 边长覆盖约 ±5,242km。
- 插入 / 更新时拦截 `NaN` / `Inf` 非法坐标并返回错误码 `6003`。

### 范围与近邻查询

- 2D 提供 `QueryRadius`、`QueryAABB`、`QuerySector`、`QueryNearest`、`QueryKNearest`；3D 提供 `QueryRadius`、`QueryAABB`、`QueryCylinder`、`QueryCone`、`QueryNearest`、`QueryKNearest`。
- 扇形 / 锥体使用紧致轴向包围盒与零开方点积平方判别；近邻使用中心格安全边距早退 + 切比雪夫壳层扩展。
- 全部查询提供 `Query*Matching` 流式接口，配合默认 `FAcceptAllPredicate` 就地消费；状态谓词至多单次求值。
- `K <= 16` 使用栈内联 `TSpatialKNearestBuffer`，零堆分配并按距离升序输出。

### Actor 分层空间索引

- `UGameSpatialSubsystem` 继承 `UWorldSubsystem`，按 `GameplayTag` 分层；空 `LayerTag` 归入静态标签 `Spatial.Layer.Default`。
- 内部以 `TObjectKey<AActor>` 持有不可变弱身份键，注册、查询与只读探查一致排除已销毁或销毁中的 Actor；`UnregisterActor` 允许在 `EndPlay` 清理期调用。
- `UpdateActorLocation` 对未位移坐标直接早退；`ClearLayer` / `PruneStaleActors` 仅遍历目标图层，`PruneAllStaleActors` 执行全局失效巡检。
- C++ 侧提供 `ForEachActorInRadius` / `ForEachActorInCone` / `ForEachActorInCylinder` / `ForEachActorInAABB` 流式遍历，以及 `QueryKNearestActors`、`GetActorsInLayer` 等批量接口。

### 视锥与重要度节流

- `FSpatialThrottleDriver` 可绑定 `FThrottledTickScheduler` 或 `UThrottledTickerSubsystem`；绑定子系统时通过弱引用跟踪，避免子系统先行销毁造成悬垂。
- 同时受 `Config.DistanceFar` 与 `ViewContext.MaxDistance` 双上限约束；`BuildViewParams` 每帧预计算四档双向迟滞阈值平方，热循环无 `switch`、无开方、无除法。
- `RegisterSpatialEntity` / `UnregisterSpatialEntity` / `SetEntitySignificance` 以句柄 O(1) 管理实体；高重要度实体保持 Near 档间隔。
- 更新期重入通过 `PendingAdditions` 队列处理，避免热路径 `TFunction` 拷贝。

### 日志与错误码

- 私有依赖 `GameLogging`，统一分类 `LogGameSpatial`，前缀包括 `[GameSpatial]`、`[SpatialSubsystem]`、`[ThrottleDriver]`、`[SpatialPerf]`。
- 模块启停输出 `Log` 里程碑；销毁、递归重入、非法视图、非法注册坐标输出单次或限频 `Warning`；注册拒绝与惰性清理输出 `Verbose`；高频更新与查询热路径静默。
- 错误码定义于 [SpatialErrorCodes.h](Source/GameSpatial/Public/Core/SpatialErrorCodes.h)，归属 `Game::ModuleId::Spatial (6)`：

| 错误码 | 标识符 | 含义 |
| :---: | :--- | :--- |
| `6001` | `ElementNotFound` | 目标元素未注册或已移除（`Update` / `Remove`） |
| `6002` | `ElementAlreadyExists` | 重复插入被拦截（`Insert`） |
| `6003` | `InvalidCoordinate` | 坐标含 `NaN` / `Inf` 非有限分量 |

## 架构与依赖

| 依赖 | 可见性 | 用途 |
| :--- | :---: | :--- |
| `GameCore` | Public | 哈希、`TGameStatusResult`、`FThrottledTickScheduler` / `UThrottledTickerSubsystem` |
| `GameLogging` | Private | `LogGameSpatial` 分类日志 |
| `Core` / `CoreUObject` / `Engine` / `GameplayTags` | Public | Actor、`UWorldSubsystem`、`FGameplayTag` |

| 关键类型 | 角色 |
| :--- | :--- |
| `TSpatialHashGrid2D/3D<TElement>` | 唯一公开网格门面 |
| `UGameSpatialSubsystem` | 世界级 Actor 分层索引 |
| `FSpatialThrottleDriver` | 距离 / 视锥档位到 Tick 周期 |
| `Game::SpatialErrorCodes` | 6001~6003 错误契约 |

<details>
<summary><b>展开目录结构</b></summary>

```text
Plugins/GameSpatial/
├── Docs/
│   ├── Architecture/Overview.md                # 物理分层、收缩门面、自适应壳层扩展与节流架构说明
│   ├── API/SpatialReference.md                 # 全量公共 API 与错误码契约手册
│   └── Guides/QuickStart.md                    # C++ / 蓝图接入示例与性能最佳实践
├── Source/
│   └── GameSpatial/
│       ├── Public/
│       │   ├── Core/
│       │   │   ├── SpatialErrorCodes.h         # Game::SpatialErrorCodes (6001 ~ 6003)
│       │   │   ├── SpatialGameplayTags.h       # TAG_Spatial_Layer_Default 原生标签声明
│       │   │   └── SpatialQueryCommon.h        # 雪崩散列键策略、无开方锥体数学与谓词适配器
│       │   ├── Grid/
│       │   │   ├── Internal/                   # Game::Spatial::Detail 内部实现零件（不作为公共入口）
│       │   │   │   ├── SpatialCoordPolicies.h
│       │   │   │   ├── TSpatialHashContainerBase.h
│       │   │   │   ├── TSpatialKNearestBuffer.h
│       │   │   │   ├── FSpatialQuery2D.h
│       │   │   │   ├── FSpatialQuery3D.h
│       │   │   │   ├── FSpatialNearestQuery2D.h
│       │   │   │   └── FSpatialNearestQuery3D.h
│       │   │   ├── TSpatialHashGrid2D.h        # 2D 唯一公开网格门面
│       │   │   └── TSpatialHashGrid3D.h        # 3D 唯一公开网格门面
│       │   ├── Throttle/
│       │   │   ├── FSpatialViewContext.h
│       │   │   ├── FSpatialThrottleTierConfig.h
│       │   │   └── FSpatialThrottleDriver.h
│       │   ├── Subsystem/
│       │   │   └── UGameSpatialSubsystem.h
│       │   └── GameSpatial.h                   # 模块入口与收敛公共契约聚合门面头
│       ├── Private/
│       │   ├── Core/SpatialGameplayTags.cpp
│       │   ├── Throttle/FSpatialThrottleDriver.cpp
│       │   ├── Subsystem/UGameSpatialSubsystem.cpp
│       │   ├── Tests/                          # 18 项自动化功能与 40 项微基准回归套件
│       │   │   ├── SpatialTestHelpers.h
│       │   │   ├── TSpatialHashGrid2DTest.cpp
│       │   │   ├── TSpatialHashGrid3DTest.cpp
│       │   │   ├── FSpatialThrottleDriverTest.cpp
│       │   │   ├── GameSpatialSubsystemTest.cpp
│       │   │   └── SpatialPerformanceTest.cpp
│       │   └── GameSpatial.cpp
│       └── GameSpatial.Build.cs
├── GameSpatial.uplugin
└── README.md
```

</details>

## 性能与边界

<details>
<summary>展开性能基准（历史 Development 构建参考值）</summary>

实测环境：Apple Silicon（macOS arm64，UE 5.8 Development 构建，Apple Clang 21.1.6，2 轮预热 + 7 轮采样中位数，基线提交 `54f7a2f`，复现命令 `python3 .scripts/run_automation_tests.py --case Game.Spatial`）：

| 基准项 | 中位数耗时 | 离散度 (CV%) | 吞吐量 |
|---|---:|---:|---:|
| 2D 同格 / 跨格 O(1) 真实更新 | `10.19 / 19.47 ns/op` | `0.93% / 6.65%` | `98.17 / 51.35 Mops/s` |
| 2D `Remove` + `Insert` 往返 / 高密度单桶(64) 更新 | `27.80 / 4.21 ns/op` | `1.32% / 1.29%` | `35.98 / 237.62 Mops/s` |
| 2D 分布式千实体流式半径 / 5,000 实体大规模流式半径 | `63.46 / 62.46 ns/op` | `2.10% / 1.04%` | `15.76 / 16.01 Mops/s` |
| 2D 分布式千实体 AABB / 30° 紧致扇形查询 | `71.70 / 77.10 ns/op` | `2.89% / 1.81%` | `13.95 / 12.97 Mops/s` |
| 2D 分布式千实体最近邻 / 空中心格大半径近邻 | `18.91 / 45.81 ns/op` | `3.10% / 1.14%` | `52.87 / 21.83 Mops/s` |
| 2D 分布式千实体 K=5 / K=16 / K=32 近邻（向外壳层） | `117.09 / 376.96 / 853.43 ns/op` | `1.41% / 0.74% / 0.13%` | `8.54 / 2.65 / 1.17 Mops/s` |
| 3D 同格 / 跨格 / 高密度单桶(64) O(1) 更新 | `10.05 / 19.41 / 5.12 ns/op` | `2.15% / 6.75% / 0.38%` | `99.54 / 51.52 / 195.22 Mops/s` |
| 3D 分布式千实体球形 / AABB / 圆柱体 / 30° 紧致锥体 | `468.37 / 157.49 / 176.87 / 194.65 ns/op` | `0.62% / 0.33% / 6.06% / 1.73%` | `2.14 / 6.35 / 5.65 / 5.14 Mops/s` |
| 3D 分布式千实体最近邻 / 空中心格大半径近邻 | `62.29 / 271.70 ns/op` | `1.23% / 1.17%` | `16.05 / 3.68 Mops/s` |
| 3D 分布式千实体 K=5 / K=16 / K=32 近邻（向外壳层） | `194.32 / 622.30 / 1,686.91 ns/op` | `1.20% / 0.66% / 0.25%` | `5.15 / 1.61 / 0.59 Mops/s` |
| 3D 平面地图 Z=0（1,000 桶）/ 5,000 实体大规模流式球形 | `119.73 / 217.43 ns/op` | `2.85% / 0.84%` | `8.35 / 4.60 Mops/s` |
| 3D 稀疏图超大半径（500m / 100 实体）自适应近邻 | `146.77 ns/op` | `0.85%` | `6.81 Mops/s` |
| 子系统千级 Actor 静止早退 / 真实位移更新 | `11.60 / 26.11 ns/op` | `3.35% / 0.77%` | `86.22 / 38.29 Mops/s` |
| 子系统千级 Actor 流式球形 / 锥体 / 圆柱 / AABB | `264.00 / 152.08 / 238.50 / 206.37 ns/op` | `0.57% / 1.61% / 2.84% / 1.09%` | `3.79 / 6.58 / 4.19 / 4.85 Mops/s` |
| 子系统千级 Actor K=5 近邻 / 全局失效巡检 (PruneAll) | `148.33 / 2,906.70 ns/op` | `0.62% / 1.12%` | `6.74 Mops/s / 344.0 Kops/s` |
| 节流驱动器 500 实体稳态 / 动态旋转视锥评估 | `2,787.92 / 5,118.00 ns/op` | `0.46% / 0.44%` | `358.69 / 195.39 Kops/s` |
| 节流驱动器单实体注册 + 注销往返 | `448.25 ns/op` | `0.74%` | `2.23 Mops/s` |

</details>

- **线程**：`UGameSpatialSubsystem` 与 `FSpatialThrottleDriver` 均非线程安全，所有调用必须在 GameThread；网格模板也不做内部加锁。
- **生命周期**：Actor 索引随 `UWorld` 创建与销毁，`Deinitialize` 清空全部图层与追踪表，不跨世界保留状态。
- **坐标范围**：3D 网格默认 `500cm` 边长覆盖约 ±5,242km；超出范围或传入 `NaN` / `Inf` 会命中 `6003`，需要调整 `CellSize` 或拆分网格。
- **查询规模**：仅 `K <= 16` 使用栈内联零堆分配缓冲，更大的 K 不享受该路径。
- **错误契约**：`Insert` 重复返回 `6002`，`Update` / `Remove` 未命中返回 `6001`。
- **性能数据**：均为历史 Development 实测参考值，未纳入 CI 自动复测，不构成 Shipping 性能承诺。

## 文档导航

| 文档 | 内容 |
| :--- | :--- |
| [架构概述](Docs/Architecture/Overview.md) | 物理分层、收缩门面、自适应壳层扩展与节流架构 |
| [API 参考](Docs/API/SpatialReference.md) | 全量公共 API 与错误码契约 |
| [快速开始](Docs/Guides/QuickStart.md) | C++ / 蓝图接入示例与性能最佳实践 |

自动化回归：`python3 .scripts/run_automation_tests.py --case Game.Spatial`（18 项功能测试 + 40 项微基准）。
