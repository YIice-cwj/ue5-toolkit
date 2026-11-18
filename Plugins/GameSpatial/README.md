# GameSpatial 插件

**GameSpatial** 是面向 Gameplay、战斗逻辑、视锥剔除与 AI 感知的纯逻辑轻量空间几何检索基础设施插件。它**完全脱离 PhysX / Chaos 物理引擎依赖**，提供纳秒级 2D/3D 空间哈希网格、世界级 Actor 分层空间索引子系统与视野重要度节流驱动器。

---

## 一、 核心功能与架构设计

### 1. 2D 均匀空间哈希网格 (`TSpatialHashGrid2D<TElement>`)
- **纯逻辑无碰撞哈希**：基于 `uint64` 双 32 位整型无碰撞直接位拼接（`(UX << 32) | UY`），纳秒级单元格寻址；
- **动态实体增量迁移**：同单元格内坐标微移零桶迁移开销，跨桶更新原子维护；
- **多形态几何范围查询**：
  - **半径圆形查询** (`QueryRadius`)
  - **轴对齐矩形包围盒查询** (`QueryAABB`)
  - **扇形前向范围查询** (`QuerySector`，带半角余弦判定与角隅 AABB 剪枝，常用于 2D/俯视角攻击扇面与前向视野)
  - **最近邻查找** (`QueryNearest`，中心单元格优先探测收缩 + 角隅最短距离动态剪枝)
  - **K 近邻多目标检索** (`QueryKNearest`，维护升序候选堆并自适应收缩截断上界)
- **现代 C++20 谓词体系**：全系查询支持可调用对象过滤（自动适配 `(Item)` 或 `(Item, Loc)` 双签名），原地过滤零额外内存分配。

### 2. 3D 均匀空间哈希网格 (`TSpatialHashGrid3D<TElement>`)
- **21 位高维坐标位压缩**：采用 21 位偏置位压缩技术（`COORD_BITS = 21`，偏移量 $2^{20}$），将三维网格坐标压缩至单个 64 位整型（`UX << 42 | UY << 21 | UZ`）；
- **开放世界超大地图支持**：默认 500.0f 边长下，三轴覆盖范围达 $\pm 524$ 公里，100% 确定性无哈希碰撞；
- **全方位空间立体查询**：
  - **球形立体查询** (`QueryRadius`)
  - **三维包围盒查询** (`QueryAABB`)
  - **直立圆柱体查询** (`QueryCylinder`，地面战斗判定与垂直范围伤害神器)
  - **锥体范围查询** (`QueryCone`，视锥范围、手电筒光锥与 3D 前向攻击范围判定)
  - **三维最近邻查找** (`QueryNearest`，三级轴向包围盒动态剪枝)
  - **三维 K 近邻检索** (`QueryKNearest`，升序排列多目标检索)
- **角隅动态剪枝技术**：三维空间外接球中有约 47.6% 的单元格完全落在球体外，通过三级包围盒最短距离投影剪枝（$D_x^2$ 剪面，$D_x^2+D_y^2$ 剪线，$D_x^2+D_y^2+D_z^2$ 剪格），压降近一半的哈希查桶与点积开销。

### 3. 世界级 Actor 空间索引子系统 (`UGameSpatialSubsystem`)
- **无物理开销的高频检索**：继承自 `UWorldSubsystem`，在客户端与服务端均可独立运行，无需 PhysX / Chaos 物理碰撞体；
- **GameplayTag 分层隔离**：基于 `FGameplayTag`（如 `Spatial.Layer.Enemy`、`Spatial.Layer.Item`）实现图层物理隔离；传入空 Tag 自动归入 `Spatial.Layer.Default`；
- **GC 友好与弱引用防悬垂**：内部统一采用 `TWeakObjectPtr<AActor>`，绝对不阻碍引擎垃圾回收；查询时自动剔除失效 Actor，并提供 `PruneStaleActors` 惰性清理。

### 4. 空间检索蓝图函数库 (`UGameSpatialBlueprintLibrary`)
- 为蓝图提供基于 `WorldContextObject` 的纯静态节点（`QuerySpatialActorsInRadius`、`QuerySpatialActorsInCone`、`QueryNearestSpatialActor` 等），一键融入蓝图工作流。

### 5. 空间视锥节流驱动器 (`FSpatialThrottleDriver`)
- 协同 `GameCore` 插件的 `FThrottledTickScheduler` / `UThrottledTickerSubsystem`；
- 集中批量评估观察者/摄像机视锥投影（FOV 视锥包含度与距离分级 Near / Mid / Far / OutOfRange）；
- **双向迟滞带保护 (Hysteresis)**：提供角度迟滞与距离迟滞带，彻底消除实体在边界处的乒乓跳频抽搐；
- **高鲁棒性与防悬垂**：弱引用绑定世界子系统，全面支持在更新与坐标获取回调中安全重入注销或批量扩容。

---

## 二、 目录结构

```
Plugins/GameSpatial/
├── Source/
│   └── GameSpatial/
│       ├── Public/
│       │   ├── Library/
│       │   │   └── UGameSpatialBlueprintLibrary.h
│       │   ├── Spatial/
│       │   │   ├── Algorithms/
│       │   │   │   ├── SpatialQuery2D.h
│       │   │   │   └── SpatialQuery3D.h
│       │   │   ├── Containers/
│       │   │   │   ├── TSpatialHashContainer2D.h
│       │   │   │   └── TSpatialHashContainer3D.h
│       │   │   ├── FSpatialThrottleDriver.h
│       │   │   ├── SpatialErrorCodes.h
│       │   │   ├── TSpatialHashGrid2D.h
│       │   │   └── TSpatialHashGrid3D.h
│       │   ├── Subsystem/
│       │   │   └── UGameSpatialSubsystem.h
│       │   └── GameSpatial.h
│       ├── Private/
│       │   ├── Library/
│       │   │   └── UGameSpatialBlueprintLibrary.cpp
│       │   ├── Spatial/
│       │   │   └── FSpatialThrottleDriver.cpp
│       │   ├── Subsystem/
│       │   │   └── UGameSpatialSubsystem.cpp
│       │   ├── Tests/
│       │   │   ├── FSpatialThrottleDriverTest.cpp
│       │   │   ├── GameSpatialSubsystemTest.cpp
│       │   │   ├── SpatialPerformanceTest.cpp
│       │   │   ├── TSpatialHashGrid2DTest.cpp
│       │   │   └── TSpatialHashGrid3DTest.cpp
│       │   └── GameSpatial.cpp
│       └── GameSpatial.Build.cs
├── GameSpatial.uplugin
└── README.md
```

---

## 三、 使用示例

### 1. 2D 空间网格与扇形/近邻检索
```cpp
// 初始化 500 边长的 2D 网格
TSpatialHashGrid2D<int32> UnitGrid(500.0f);

// 插入单位
UnitGrid.Insert(UnitIdA, FVector2D(100.0f, 200.0f));
UnitGrid.Insert(UnitIdB, FVector2D(150.0f, 220.0f));

// 扇形前向范围查询（朝向 (1, 0)，半径 600，半开角 45°）
TArray<int32> EnemiesInSector;
UnitGrid.QuerySector(PlayerPos2D, FVector2D(1.0f, 0.0f), 600.0f, 45.0f, EnemiesInSector, [](int32 Id) {
    return IsHostile(Id);
});

// 多目标 K 近邻检索（查找最近的 3 个存活友军）
TArray<int32> NearestAllies;
UnitGrid.QueryKNearest(PlayerPos2D, 2000.0f, 3, NearestAllies, [](int32 Id) {
    return IsAlive(Id);
});
```

### 2. 3D 空间网格与锥体/圆柱体检索
```cpp
TSpatialHashGrid3D<int32> SpatialGrid(500.0f);

// 插入目标
SpatialGrid.Insert(TargetId, TargetLocation);

// 地面战斗圆柱体范围判定（半径 800，上下高度各 150）
TArray<int32> HitTargets;
SpatialGrid.QueryCylinder(ImpactCenter, 800.0f, 150.0f, HitTargets);

// 视锥/前向光锥查询（顶点 Apex，中轴方向 Direction，距离 1200，顶角半角 30°）
TArray<int32> TargetsInCone;
SpatialGrid.QueryCone(Apex, Direction, 1200.0f, 30.0f, TargetsInCone);
```

### 3. 世界子系统 `UGameSpatialSubsystem` 管理 Actor
```cpp
// 获取世界子系统
UGameSpatialSubsystem* SpatialSubsystem = World->GetSubsystem<UGameSpatialSubsystem>();

// 按标签注册 Actor（敌对层）
const FGameplayTag EnemyLayer = FGameplayTag::RequestGameplayTag(FName("Spatial.Layer.Enemy"));
SpatialSubsystem->RegisterActor(EnemyActor, EnemyLayer);

// 广播或技能时查询半径内的敌对 Actor
TArray<AActor*> NearbyEnemies;
SpatialSubsystem->QueryActorsInRadius(PlayerLocation, 1000.0f, EnemyLayer, NearbyEnemies);

// 查找最近的单个敌对 Actor
AActor* NearestEnemy = nullptr;
if (SpatialSubsystem->QueryNearestActor(PlayerLocation, 2500.0f, EnemyLayer, NearestEnemy)) {
    // 锁定目标...
}
```

### 4. 零堆分配流式就地消费查询 (`QueryXXXMatching`)
针对高频循环与海量实体交互场景，提供就地流式消费接口，避免中间数组构造与堆内存分配开销：
```cpp
// 零堆分配就地遍历累加伤害或状态
SpatialGrid.QueryRadiusMatching(ExplosionCenter, 500.0f, [](int32 TargetId, const FVector& Loc) {
    ApplyRadialDamage(TargetId, Loc);
});
```

### 5. 蓝图中使用静态函数库
在蓝图事件图表中直接搜索并调用：
- **Actor 生命周期与位置**：
  - `Register Spatial Actor`（带濒死销毁状态保护）
  - `Register Spatial Actors`（批量注册）
  - `Unregister Spatial Actor`
  - `Unregister Spatial Actors`（批量注销）
  - `Update Spatial Actor Location`
  - `Update Spatial Actor`
  - `Is Spatial Actor Registered`
  - `Get Spatial Actor Layer`（图层反查）
  - `Get Spatial Actor Location`（最后同步空间坐标反查）
- **几何范围与目标检索**：
  - `Query Spatial Actors In Radius`
  - `Query Spatial Actors In Cone`
  - `Query Spatial Actors In Cylinder`
  - `Query Spatial Actors In AABB`
  - `Query Nearest Spatial Actor`
  - `Query K Nearest Spatial Actors`
  - `Get Spatial Actors In Layer`（全图层有效实体导出）
- **图层管理与弱引用清理**：
  - `Clear Spatial Layer`
  - `Clear All Spatial Layers`
  - `Prune Spatial Stale Actors`（单图层失效弱引用清理）
  - `Prune All Spatial Stale Actors`（跨图层全局失效弱引用清理）
  - `Get Spatial Actor Count`
  - `Get Total Spatial Actor Count`

---

## 四、 错误码规范 (`Game::SpatialErrorCodes`)

插件全量统一使用 `GameCore` 的 `TGameStatusResult<T>` 契约体系，错误码定义于 `Public/Spatial/SpatialErrorCodes.h`：

| 错误码 | 标识符 | 含义与典型场景 |
|:---:|---|---|
| `6001` | `ElementNotFound` | 目标空间元素未在网格中注册或已注销（如 `GetLocation`、`Update`、`Remove` 不存在元素） |
| `6002` | `ElementAlreadyExists` | 目标空间元素已被占用，拦截重复插入（如 `Insert` 重复元素） |
| `6003` | `InvalidQueryParameter` | 查询几何参数非法（如检索半径 $\le 0$、颠倒的 AABB 包围盒等） |

---

## 五、 性能基准

在 Apple M3 Pro 平台下的基准测试结果（`SpatialPerformanceTest.cpp`）：
- **2D/3D 网格更新**：$< 800\text{ ns/op}$（超过 $1,250,000\text{ ops/s}$）；
- **2D/3D 球形/扇形/锥体查询**：$1000 \sim 2500\text{ ns/op}$（超过 $500,000\text{ ops/s}$）；
- **动态剪枝最近邻/K 近邻查询**：$1200 \sim 2800\text{ ns/op}$（超过 $400,000\text{ ops/s}$）；
- **视锥节流驱动器**：500 实体集中批量投影与迟滞带评估 $< 20\ \mu\text{s}$；
- **Actor 空间子系统 (1000 活跃 Actor)**：高频坐标更新 $< 3000\text{ ns/op}$，零分配球形流式查询 $< 8000\text{ ns/op}$。
