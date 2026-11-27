# GameSpatial 插件

**GameSpatial**（v2.0.0）是面向 Unreal Engine 5（UE 5.4 ~ 5.8）的纯逻辑轻量空间几何检索与视锥节流基础设施插件。它**完全脱离 PhysX / Chaos 物理引擎依赖**，公开仅依赖 `GameCore` 与引擎基础模块，私有单向接入 `GameLogging`（`LogGameSpatial`），严格遵守单向无环分层矩阵（**零 `GamePool` 或横向 Level 1 依赖**）。

---

## 一、 核心功能与架构特性

1. **2D / 3D 均匀空间哈希网格 (`TSpatialHashGrid2D<TElement>` / `TSpatialHashGrid3D<TElement>`)**
   - **收敛唯一公开门面与共享底层基类**：底层存储 `TSpatialHashContainerBase`、坐标打包策略、栈内联 K 近邻缓冲与几何算法全部收拢至 `Public/Grid/Internal/`（`Game::Spatial::Detail` 命名空间），对外仅暴露 `TSpatialHashGrid2D/3D` 唯一入口；
   - **抗碰撞 64→32 雪崩散列与单次探查插入**：复用 `GameCore::Game::Hash::Hash64To32` 定制单元格键策略消除平面格网桶聚集；`Insert` 通过 `FindOrAdd` 单次哈希探查完成查重与写入，同格原地直写、跨格与移除均采用 O(1) Swap-Pop 并自动回收空桶；
   - **UE5 LWC 全双精度映射**：底层统一以 `double InvCellSize` 与 `FloorToClampedCellCoord` 计算格网索引，3D 采用 21 位有符号偏置压缩（默认 `500cm` 边长覆盖 ±524km），拦截 `NaN`/`Inf` 非法坐标（`6003`）；
   - **紧致剪枝、零开方求交与自适应向外壳层近邻**：
     - 2D：`QueryRadius`、`QueryAABB`、`QuerySector`、`QueryNearest`、`QueryKNearest`；
     - 3D：`QueryRadius`、`QueryAABB`、`QueryCylinder`、`QueryCone`、`QueryNearest`、`QueryKNearest`；
     - 范围查询（`FSpatialQuery2D/3D`）与近邻查询（`FSpatialNearestQuery2D/3D`）职责分离：扇形/锥体采用**精确轴向紧致包围盒**与**零开方点积平方判别**（含 90° 斜向法平面浮点容差）；近邻与 K 近邻采用**中心格安全边距早退 + 切比雪夫壳层扩展 + 零浮点位解包稀疏回退**（保证有状态 `mutable` 谓词至多单次求值）；`K <= 16` 采用独立 `TSpatialKNearestBuffer` 栈内联缓冲零堆分配；
   - **统一默认谓词与零分配流式原语**：全部查询以编译期零开销 `FAcceptAllPredicate` 统一重载，配套 `Query*Matching` 就地流式消费接口与 `IsEmpty()` 状态探查。

2. **世界级 Actor 分层空间索引子系统 (`UGameSpatialSubsystem`)**
   - 继承自 `UWorldSubsystem`，同时面向蓝图与 C++ 统一暴露；
   - **静态原生默认标签**：通过 `UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Spatial_Layer_Default, "Spatial.Layer.Default", ...)` 静态注册，空 `LayerTag` 自动归入默认层；
   - **不可变弱身份键 GC 安全与静止坐标早退**：内部统一采用 `TObjectKey<AActor>` 杜绝已销毁弱指针哈希桶碰撞误删；`UpdateActorLocation` 与同层重注册对未位移坐标直接早退（省去图层与网格双重查找）；销毁态 Actor 在检索与只读探查中一致排除，`UnregisterActor` 放行销毁期调用以支持 `EndPlay` 清理；
   - **O(LayerSize) 精准清理与 C++ 流式查询**：`ClearLayer` / `PruneStaleActors` 仅遍历目标图层；`ForEachActorInRadius/Cone/Cylinder/AABB` 与零中间数组分配的 `QueryKNearestActors` / `GetActorsInLayer`。

3. **视锥与重要度心跳节流驱动器 (`FSpatialThrottleDriver`)**
   - 协同 `GameCore` 的 `FThrottledTickScheduler` / `UThrottledTickerSubsystem`（弱引用追踪防悬垂）；
   - 同时生效 `Config.DistanceFar` 与 `ViewContext.MaxDistance` 双上限，在 `BuildViewParams` 每帧预计算全部四档双向迟滞距离阈值平方查表，热循环零 `switch`、零开方、零除法；
   - 基于 `PendingAdditions` 重入队列消除评估热路径 `TFunction` 拷贝，基于 `HandleToIndex` 实现驱动器侧 O(1) 注销、重要度切换与注册态查询。

4. **分级可观测日志 (`LogGameSpatial`)**
   - 私有接入 `GameLogging`，统一 `[GameSpatial]` / `[SpatialSubsystem]` / `[ThrottleDriver]` / `[SpatialPerf]` 前缀；模块启停输出 `Log` 里程碑，子系统销毁/递归重入/非法视图/非法注册坐标输出单次或限频 `Warning`，注册拒绝与惰性清理输出 `Verbose`，高频更新与查询热路径 100% 静默。

---

## 二、 目录结构

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

---

## 五、 自动化测试与延伸文档

```bash
python3 .scripts/run_automation_tests.py --case Game.Spatial
```

- [架构概述与核心机制](Docs/Architecture/Overview.md)
- [公共 API 参考手册](Docs/API/SpatialReference.md)
- [快速上手与最佳实践指南](Docs/Guides/QuickStart.md)
