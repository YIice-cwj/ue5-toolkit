# GameSpatial 公共 API 参考手册

---

## 一、 核心契约与标签 (`Public/Core/`)

### 1. `Game::SpatialErrorCodes` (`Core/SpatialErrorCodes.h`)
全部失败结果均归属 `Game::ModuleId::Spatial (6)`，配合 `TGameStatusResult<void>` 使用：

| 常量名 | 数值 | 触发场景 |
|---|:---:|---|
| `ElementNotFound` | `6001` | `Update` / `Remove` 目标元素未在网格中注册 |
| `ElementAlreadyExists` | `6002` | `Insert` 目标元素已存在，拦截重复插入 |
| `InvalidCoordinate` | `6003` | `Insert` / `Update` 传入包含 `NaN` 或 `Inf` 的非法浮点坐标 |

### 2. `TAG_Spatial_Layer_Default` (`Core/SpatialGameplayTags.h`)
- 静态原生标签 `Spatial.Layer.Default`，当向 `UGameSpatialSubsystem` 传入空 `FGameplayTag()` 时自动归入/查询该默认图层。

---

## 二、 2D / 3D 均匀空间哈希网格 (`Public/Grid/`)

对外仅暴露 `TSpatialHashGrid2D<TElement>` 与 `TSpatialHashGrid3D<TElement>` 两个收敛门面（底层存储与查询算法封装于 `Grid/Internal/` 的 `Game::Spatial::Detail` 命名空间）。

### 1. 通用生命周期与状态方法 (`TSpatialHashGrid2D<TElement>` / `TSpatialHashGrid3D<TElement>`)
- `explicit TSpatialHashGrid2D/3D(float InCellSize = 500.0f)`：非有限或 `<= 1.0f` 时安全回退至 `500.0f`。
- `void Reserve(int32 ExpectedElementCount)`：预分配元素索引表容量。
- `TGameStatusResult<void> Insert(const TElement& Element, const FVector2D/FVector& Location)`（单次元素表哈希探查）
- `TGameStatusResult<void> Update(const TElement& Element, const FVector2D/FVector& NewLocation)`（同格/跨格均 O(1)）
- `TGameStatusResult<void> Remove(const TElement& Element)`（O(1) 交换移除并自动回收空桶）
- `void Clear()`
- `bool Contains(const TElement& Element) const noexcept`
- `const FVector2D/FVector* FindLocation(const TElement& Element) const noexcept`（零开销只读探查）
- `int32 Num() const noexcept` / `bool IsEmpty() const noexcept` / `int32 GetBucketCount() const noexcept` / `float GetCellSize() const noexcept`
- `void GetAllElements(TArray<TElement>& OutElements) const`
- `template <typename TFunc> void ForEachElement(TFunc&& Func) const`（支持 `(Item)` 或 `(Item, Loc)` 签名）

### 2. 几何范围与近邻查询（统一默认全通谓词 `TPredicate = Game::Spatial::FAcceptAllPredicate`）
所有查询均支持可选过滤谓词（自动适配 `(const TElement&)` 或 `(const TElement&, const TVector&)`，支持 `mutable` 谓词且保证每元素至多求值一次），流式版本 `Query*Matching` 支持 `(const TElement&)` 或 `(const TElement&, const TVector&)`（K 近邻流式支持 `(const TElement&, double DistSq)`）：

- **2D (`TSpatialHashGrid2D`)**：
  - `QueryRadius` / `QueryRadiusMatching`
  - `QueryAABB` / `QueryAABBMatching`
  - `QuerySector` / `QuerySectorMatching`（零朝向/非正半角安全返回空；`HalfAngle >= 180°` 退化为全圆；含 90° 斜向法线浮点容差）
  - `QueryNearest`（中心向外切比雪夫壳层扩展 + 自适应稀疏回退）
  - `QueryKNearest` / `QueryKNearestMatching`（按距离由近至远升序，K <= 16 零堆分配，按实际命中数精准预留输出数组）
- **3D (`TSpatialHashGrid3D`)**：
  - `QueryRadius` / `QueryRadiusMatching`
  - `QueryAABB` / `QueryAABBMatching`
  - `QueryCylinder` / `QueryCylinderMatching`
  - `QueryCone` / `QueryConeMatching`（紧致 3D 球冠包围盒 + 零开方角判定 + 90° 法平面容差）
  - `QueryNearest`（三维切比雪夫向外壳层扩展 + 自适应稀疏回退）
  - `QueryKNearest` / `QueryKNearestMatching`

---

## 三、 世界级 Actor 空间索引子系统 (`Public/Subsystem/UGameSpatialSubsystem.h`)

继承自 `UWorldSubsystem`，同时直接暴露给蓝图与 C++（所有带 `LayerTag` 参数的接口统一采用末尾默认参数 `FGameplayTag LayerTag = FGameplayTag()`）：

- **生命周期与坐标同步**：
  - `RegisterActor(Actor, LayerTag)` / `RegisterActors(Actors, LayerTag)`（同层静止直接早退）
  - `UnregisterActor(Actor)` / `UnregisterActors(Actors)`（支持在 `EndPlay` 销毁期正常反注册）
  - `UpdateActorLocation(Actor, NewLocation)` / `UpdateActor(Actor)`（静止未变化时跳过图层网格查找直接早退）
- **蓝图/C++ 通用几何检索**：
  - `QueryActorsInRadius(Center, Radius, OutActors, LayerTag)`
  - `QueryActorsInCone(Apex, Direction, Radius, HalfAngleDegrees, OutActors, LayerTag)`
  - `QueryActorsInCylinder(Center, Radius, HalfHeight, OutActors, LayerTag)`
  - `QueryActorsInAABB(Box, OutActors, LayerTag)`
  - `QueryNearestActor(Center, MaxSearchRadius, OutNearestActor, LayerTag) -> bool`（未找到时将 `OutNearestActor` 重置为 `nullptr` 并返回 `false`）
  - `QueryKNearestActors(Center, MaxSearchRadius, K, OutActors, LayerTag)`
- **C++ 零堆分配流式原语**（自动过滤已销毁或销毁中条目）：
  - `ForEachActorInRadius` / `ForEachActorInCone` / `ForEachActorInCylinder` / `ForEachActorInAABB`
- **图层管理、反查与失效清理**：
  - `ClearLayer(LayerTag)` / `ClearAll()`
  - `PruneStaleActors(LayerTag)` / `PruneAllStaleActors()`
  - `GetActorCount(LayerTag)` / `GetTotalActorCount()`
  - `IsActorRegistered(Actor)` / `GetActorLayer(Actor)` / `GetActorLocation(Actor, OutLocation)` / `GetActorsInLayer(OutActors, LayerTag)`
  - `static FGameplayTag GetDefaultLayerTag() noexcept`

---

## 四、 视锥与重要度心跳节流驱动器 (`Public/Throttle/`)

- **`FSpatialViewContext`** (`FSpatialViewContext.h`)：`ViewLocation`、`ViewForward`、`FOVDegrees`、`MaxDistance`。
- **`FSpatialThrottleTierConfig`** (`FSpatialThrottleTierConfig.h`)：`DistanceNear`、`DistanceMid`、`DistanceFar`、`DistanceHysteresis`、`NearInterval`、`MidInterval`、`FarInterval`、`OutOfRangeOrBehindInterval`、`HysteresisDegrees`。
- **`FSpatialThrottleDriver`** (`FSpatialThrottleDriver.h`)：
  - 构造绑定 `FThrottledTickScheduler&`（调用方须保证调度器生命周期长于本驱动器）或 `UThrottledTickerSubsystem&`（弱引用防悬垂）；
  - `RegisterSpatialEntity(Validator, PositionGetter, Callback, bHighSignificance = false) -> uint64`
  - `UnregisterSpatialEntity(Handle) -> bool`（驱动器侧 O(1) 句柄索引移除并同步注销底层调度器心跳）
  - `SetEntitySignificance(Handle, bHighSignificance) -> bool`（O(1)）
  - `IsEntityRegistered(Handle) -> bool`（O(1)）
  - `UpdateSpatialThrottle(ViewContext)`（每帧预计算四档阈值平方查表）
  - `SetTierConfig(Config)` / `GetTierConfig()` / `Num()` / `Clear()`
