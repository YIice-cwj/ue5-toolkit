# GameSpatial 插件

**GameSpatial**（v2.0.0）是面向 Unreal Engine 5（UE 5.1 ~ 5.8）的纯逻辑轻量空间几何检索与视锥节流基础设施插件。它**完全脱离 PhysX / Chaos 物理引擎依赖**，公开仅依赖 `GameCore` 与引擎基础模块，私有单向接入 `GameLogging`（`LogGameSpatial`），严格遵守单向无环分层矩阵（**零 `GamePool` 或横向 Level 1 依赖**）。

---

## 一、 核心功能与架构特性

1. **2D / 3D 均匀空间哈希网格 (`TSpatialHashGrid2D<TElement>` / `TSpatialHashGrid3D<TElement>`)**
   - **抗碰撞 64→32 雪崩散列**：复用 `GameCore::Game::Hash::Hash64To32` 定制单元格 `TMap` 键策略，消除 3D 平面地图（`CellZ == 0`）与 2D 周期格网的哈希桶碰撞退化；
   - **全 O(1) 增删改查**：`FElementRecord` 双向维护 `CellKey` 与 `BucketIndex`，同格微移原地直写、跨格迁移与移除采用 O(1) Swap-Pop 并自动回收空桶；
   - **UE5 LWC 全双精度映射**：底层统一以 `double InvCellSize` 与 `FloorToClampedCellCoord` 计算格网索引，3D 采用 21 位有符号偏置压缩（默认 `500cm` 边长覆盖 ±524km），拦截 `NaN`/`Inf` 非法坐标（`6003`）；
   - **紧致剪枝与零开方求交**：
     - 2D：`QueryRadius`、`QueryAABB`、`QuerySector`、`QueryNearest`、`QueryKNearest`；
     - 3D：`QueryRadius`、`QueryAABB`、`QueryCylinder`、`QueryCone`、`QueryNearest`、`QueryKNearest`；
     - 扇形/锥体查询同时采用**精确轴向紧致包围盒**与**零开方点积平方判别**；稀疏大半径自动切换直扫快路径；`K <= 16` K 近邻采用栈内联缓冲零堆分配；
   - **统一默认谓词与零分配流式原语**：全部查询以编译期零开销 `FAcceptAllPredicate` 统一重载，配套 `Query*Matching` 就地流式消费接口。

2. **世界级 Actor 分层空间索引子系统 (`UGameSpatialSubsystem`)**
   - 继承自 `UWorldSubsystem`，同时面向蓝图与 C++ 统一暴露（不再维护重复的静态蓝图函数库）；
   - **静态原生默认标签**：通过 `UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Spatial_Layer_Default, "Spatial.Layer.Default", ...)` 静态注册，空 `LayerTag` 自动归入默认层；
   - **弱引用 GC 安全与状态一致**：内部统一持有 `TWeakObjectPtr<AActor>`，销毁态 Actor 在检索与只读探查中一致排除，`UnregisterActor` 放行销毁期调用以支持 `EndPlay` 清理；
   - **O(LayerSize) 精准清理与 C++ 流式查询**：`ClearLayer` / `PruneStaleActors` 仅遍历目标图层；`ForEachActorInRadius/Cone/Cylinder/AABB` 与零中间数组分配的 `QueryKNearestActors` / `GetActorsInLayer`。

3. **视锥与重要度心跳节流驱动器 (`FSpatialThrottleDriver`)**
   - 协同 `GameCore` 的 `FThrottledTickScheduler` / `UThrottledTickerSubsystem`（弱引用追踪防悬垂）；
   - 同时生效 `Config.DistanceFar` 与 `ViewContext.MaxDistance` 双上限，支持 Near / Mid / Far / OutOfRange 四级距离迟滞带与 FOV 角度双向迟滞带；
   - 基于 `PendingAdditions` 重入队列消除评估热路径 `TFunction` 拷贝，基于 `HandleToIndex` 实现 O(1) 注销、重要度切换与注册态查询。

4. **分级可观测日志 (`LogGameSpatial`)**
   - 私有接入 `GameLogging`，模块启停输出 `Log` 里程碑，子系统销毁/递归重入/非法视图输出 `Warning`，注册拒绝与惰性清理输出 `Verbose`，高频更新与查询热路径 100% 静默。

---

## 二、 目录结构

```text
Plugins/GameSpatial/
├── Docs/
│   ├── Architecture/Overview.md                # 物理分层、雪崩散列、紧致剪枝与节流架构说明
│   ├── API/SpatialReference.md                 # 全量公共 API 与错误码契约手册
│   └── Guides/QuickStart.md                    # C++ / 蓝图接入示例与性能最佳实践
├── Source/
│   └── GameSpatial/
│       ├── Public/
│       │   ├── Core/
│       │   │   ├── SpatialErrorCodes.h         # Game::SpatialErrorCodes (6001 ~ 6003)
│       │   │   ├── SpatialGameplayTags.h       # TAG_Spatial_Layer_Default 原生标签声明
│       │   │   └── SpatialQueryCommon.h        # 雪崩键策略、锥体几何与栈内联 K 近邻缓冲
│       │   ├── Grid/
│       │   │   ├── TSpatialHashContainer2D.h
│       │   │   ├── TSpatialHashContainer3D.h
│       │   │   ├── FSpatialQuery2D.h
│       │   │   ├── FSpatialQuery3D.h
│       │   │   ├── TSpatialHashGrid2D.h
│       │   │   └── TSpatialHashGrid3D.h
│       │   ├── Throttle/
│       │   │   ├── FSpatialViewContext.h
│       │   │   ├── FSpatialThrottleTierConfig.h
│       │   │   └── FSpatialThrottleDriver.h
│       │   ├── Subsystem/
│       │   │   └── UGameSpatialSubsystem.h
│       │   └── GameSpatial.h                   # 模块入口与全量公共聚合门面头
│       ├── Private/
│       │   ├── Core/SpatialGameplayTags.cpp
│       │   ├── Throttle/FSpatialThrottleDriver.cpp
│       │   ├── Subsystem/UGameSpatialSubsystem.cpp
│       │   ├── Tests/                          # 18 项自动化功能与 25 项微基准回归套件
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

---

## 三、 错误码规范 (`Game::SpatialErrorCodes`)

全部失败结果归属 `Game::ModuleId::Spatial (6)`，定义于 [SpatialErrorCodes.h](Source/GameSpatial/Public/Core/SpatialErrorCodes.h)：

| 错误码 | 标识符 | 含义与典型场景 |
|:---:|---|---|
| `6001` | `ElementNotFound` | 目标空间元素未注册或已移除（`Update` / `Remove`） |
| `6002` | `ElementAlreadyExists` | 目标空间元素已存在，拦截重复插入（`Insert`） |
| `6003` | `InvalidCoordinate` | 输入坐标包含 `NaN` 或 `Inf` 非有限浮点分量（`Insert` / `Update`） |

---

## 四、 性能基准

实测环境：Apple M5（macOS 27.2.1 arm64，UE 5.8 Development 构建，Apple Clang 21.1.6，2 轮预热 + 7 轮采样中位数，基线提交 `e147fbd`，复现命令 `python3 .scripts/run_automation_tests.py --case Game.Spatial`）：

| 基准项 | 中位数耗时 | 离散度 (CV%) | 吞吐量 |
|---|---:|---:|---:|
| 2D 同格 O(1) 真实更新 | `10.39 ns/op` | `1.25%` | `96.27 Mops/s` |
| 2D 跨格 O(1) 迁移更新 | `18.28 ns/op` | `2.71%` | `54.69 Mops/s` |
| 2D `Remove` + `Insert` 往返 | `28.06 ns/op` | `0.83%` | `35.63 Mops/s` |
| 2D 分布式千实体流式半径查询（零分配） | `58.35 ns/op` | `0.76%` | `17.14 Mops/s` |
| 2D 分布式千实体 AABB / 30° 紧致扇形查询 | `77.07 / 73.54 ns/op` | `2.90% / 0.81%` | `12.98 / 13.60 Mops/s` |
| 2D 分布式千实体最近邻 / K=5 内联近邻 | `18.12 / 277.78 ns/op` | `5.87% / 0.96%` | `55.19 / 3.60 Mops/s` |
| 3D 同格 / 跨格 O(1) 真实更新 | `10.98 / 19.98 ns/op` | `38.12% / 3.55%` | `91.05 / 50.04 Mops/s` |
| 3D 分布式千实体球形 / AABB / 圆柱体查询 | `474.61 / 190.71 / 157.91 ns/op` | `2.21% / 5.15% / 1.93%` | `2.11 / 5.24 / 6.33 Mops/s` |
| 3D 分布式千实体 30° 紧致锥体查询 | `193.80 ns/op` | `5.15%` | `5.16 Mops/s` |
| 3D 分布式千实体最近邻 / K=5 内联近邻 | `41.22 / 797.80 ns/op` | `3.94% / 1.66%` | `24.26 / 1.25 Mops/s` |
| 3D 平面地图 Z=0（1,000 独立桶）雪崩哈希流式查询 | `113.97 ns/op` | `1.22%` | `8.77 Mops/s` |
| 3D 稀疏图超大半径（500m / 100 实体）快路径近邻 | `135.70 ns/op` | `2.28%` | `7.37 Mops/s` |
| 子系统千级 Actor 真实位移更新 | `946.17 ns/op` | `1.05%` | `1.06 Mops/s` |
| 子系统千级 Actor 零分配流式球形 / K=5 近邻 | `249.58 / 141.91 ns/op` | `1.23% / 9.45%` | `4.01 / 7.05 Mops/s` |
| 节流驱动器 500 实体稳态 / 动态旋转视锥评估 | `3,367.21 / 5,442.08 ns/op` | `2.46% / 1.39%` | `296.98 / 183.75 Kops/s` |
| 节流驱动器单实体 O(1) 注册 + 注销往返 | `441.92 ns/op` | `0.80%` | `2.26 Mops/s` |

---

## 五、 自动化测试与延伸文档

```bash
python3 .scripts/run_automation_tests.py --case Game.Spatial
```

- [架构概述与核心机制](Docs/Architecture/Overview.md)
- [公共 API 参考手册](Docs/API/SpatialReference.md)
- [快速上手与最佳实践指南](Docs/Guides/QuickStart.md)
