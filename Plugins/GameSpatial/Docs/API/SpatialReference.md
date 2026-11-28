# GameSpatial API 参考

> **一句话**：2D / 3D 均匀空间哈希网格、世界级 Actor 分层索引与视锥重要度 Tick 节流驱动器的公共 API。
> **头文件**：`GameSpatial.h`（聚合）或 `Grid/TSpatialHashGrid2D.h` / `Grid/TSpatialHashGrid3D.h` / `Subsystem/UGameSpatialSubsystem.h` / `Throttle/FSpatialThrottleDriver.h`
> **线程**：全部类型非线程安全，所有调用必须在 GameThread
> **失败语义**：网格增删改返回 `TGameStatusResult<void>` + `Game::SpatialErrorCodes`；查询返回 `bool` 或输出数组

## 30 秒速览

- **错误码**：`6001 ElementNotFound`、`6002 ElementAlreadyExists`、`6003 InvalidCoordinate`，归属 `Game::ModuleId::Spatial (6)`。
- **网格**：`TSpatialHashGrid2D<TElement>` / `TSpatialHashGrid3D<TElement>` 提供 O(1) 插入、更新、移除与零分配流式查询。
- **查询**：支持半径、AABB、2D 扇形、3D 圆柱 / 锥体、最近邻与 K 近邻；默认谓词 `Game::Spatial::FAcceptAllPredicate` 全通。
- **子系统**：`UGameSpatialSubsystem` 继承 `UWorldSubsystem`，按 `FGameplayTag` 分层；空标签归入 `Spatial.Layer.Default`。
- **节流**：`FSpatialThrottleDriver` 按距离档位、视锥包含度与重要度驱动 `FThrottledTickScheduler`；K 近邻仅 `K <= 16` 零堆分配。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `TSpatialHashGrid2D::Insert` | 插入元素与坐标 | 仅 GameThread；已存在 `6002`、坐标非法 `6003` |
| `TSpatialHashGrid2D::Update` | O(1) 更新元素坐标 | 未注册 `6001`、坐标非法 `6003` |
| `TSpatialHashGrid2D::Remove` | O(1) 交换移除并回收空桶 | 未找到 `6001` |
| `TSpatialHashGrid3D::QueryRadius` | 半径范围查询 | 非线程安全；非法参数返回空结果 |
| `TSpatialHashGrid3D::QueryCone` | 3D 锥体范围查询 | 无内部加锁；非法参数返回空结果 |
| `TSpatialHashGrid3D::QueryKNearest` | K 近邻按距离升序输出 | `K <= 16` 零堆分配，超出走堆分配 |
| `UGameSpatialSubsystem::RegisterActor` | 注册 Actor 到分层索引 | 仅 GameThread；空标签归默认层 |
| `UGameSpatialSubsystem::UnregisterActor` | 反注册 Actor | 允许在 `EndPlay` 销毁期调用 |
| `UGameSpatialSubsystem::ForEachActorInRadius` | 零堆分配流式半径遍历 | 仅 GameThread；自动过滤已销毁条目 |
| `FSpatialThrottleDriver::RegisterSpatialEntity` | 注册受节流的空间实体 | 仅 GameThread；返回 `0` 表示注册失败 |

## 错误码与图层标签

```cpp
namespace Game::SpatialErrorCodes {
    inline constexpr int32 ElementNotFound = 6001;
    inline constexpr int32 ElementAlreadyExists = 6002;
    inline constexpr int32 InvalidCoordinate = 6003;
}

GAMESPATIAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Spatial_Layer_Default);
```

| 常量 | 数值 | 触发场景 |
| :--- | :---: | :--- |
| `ElementNotFound` | `6001` | `Update` / `Remove` 目标元素未注册 |
| `ElementAlreadyExists` | `6002` | `Insert` 目标元素已存在，重复插入被拦截 |
| `InvalidCoordinate` | `6003` | `Insert` / `Update` 坐标含 `NaN` 或 `Inf` |

- **标签**：`TAG_Spatial_Layer_Default` 对应 `Spatial.Layer.Default`；向子系统传入空 `FGameplayTag()` 时自动归入 / 查询该默认图层。
- **线程**：常量与标签为只读，跨线程读取安全；具体网格 / 子系统的可变操作仍限 GameThread。
- **失败语义**：错误码通过 `TGameStatusResult<void>` 返回，不抛异常。

## 2D / 3D 均匀空间哈希网格

对外仅暴露两个收敛门面，底层容器与查询实现位于 `Grid/Internal/` 的 `Game::Spatial::Detail` 命名空间。

```cpp
template <typename TElement>
class TSpatialHashGrid2D {
public:
    static constexpr float DEFAULT_CELL_SIZE = FContainerType::DEFAULT_CELL_SIZE;

    explicit TSpatialHashGrid2D(float InCellSize = DEFAULT_CELL_SIZE) noexcept;

    void Reserve(int32 ExpectedElementCount);
    TGameStatusResult<void> Insert(const TElement& Element, const FVector2D& Location);
    TGameStatusResult<void> Update(const TElement& Element, const FVector2D& NewLocation);
    TGameStatusResult<void> Remove(const TElement& Element);
    void Clear();

    [[nodiscard]] bool Contains(const TElement& Element) const noexcept;
    [[nodiscard]] const FVector2D* FindLocation(const TElement& Element) const noexcept;
    [[nodiscard]] int32 Num() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] int32 GetBucketCount() const noexcept;
    [[nodiscard]] float GetCellSize() const noexcept;
    void GetAllElements(TArray<TElement>& OutElements) const;

    template <typename TFunc>
    void ForEachElement(TFunc&& Func) const;
};
```

`TSpatialHashGrid3D<TElement>` 接口与 2D 一一对应，仅将 `FVector2D` 替换为 `FVector`。

| 方法 | 参数 | 返回 / 失败 |
| :--- | :--- | :--- |
| 构造函数 | `InCellSize`：单元格边长 | 非有限或 `<= 1.0f` 时回退 `DEFAULT_CELL_SIZE`（`500.0f`） |
| `Insert` | `Element`、`Location` | 成功；重复 `6002`，非法坐标 `6003` |
| `Update` | `Element`、`NewLocation` | 同格 / 跨格均 O(1)；未找到 `6001`，非法坐标 `6003` |
| `Remove` | `Element` | O(1) 交换移除并回收空桶；未找到 `6001` |
| `Contains` / `FindLocation` | `Element` | 零开销只读探查，`FindLocation` 不存在返回 `nullptr` |
| `Num` / `IsEmpty` / `GetBucketCount` / `GetCellSize` | 无 | 状态查询，`noexcept` |
| `GetAllElements` / `ForEachElement` | 输出数组 / 消费者 | 消费者支持 `(Item)` 或 `(Item, Loc)` |

- **线程**：网格不做内部加锁，增删改查不得并发调用。
- **生命周期**：值语义，支持拷贝与移动；析构释放内部桶和索引，不持有外部资源。
- **失败语义**：增删改通过 `TGameStatusResult<void>` 返回错误码；查询不返回错误码。

### 范围查询

```cpp
// 2D
template <typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QueryRadius(const FVector2D& Center, float Radius, TArray<TElement>& OutResults, TPredicate&& Predicate = {}) const;

template <typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QueryRadiusMatching(const FVector2D& Center, float Radius, TConsumer&& Consumer, TPredicate&& Predicate = {}) const;

template <typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QueryAABB(const FBox2D& Box, TArray<TElement>& OutResults, TPredicate&& Predicate = {}) const;

template <typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QueryAABBMatching(const FBox2D& Box, TConsumer&& Consumer, TPredicate&& Predicate = {}) const;

template <typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QuerySector(const FVector2D& Center, const FVector2D& ForwardDir, float Radius, float HalfAngleDegrees, TArray<TElement>& OutResults, TPredicate&& Predicate = {}) const;

template <typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QuerySectorMatching(const FVector2D& Center, const FVector2D& ForwardDir, float Radius, float HalfAngleDegrees, TConsumer&& Consumer, TPredicate&& Predicate = {}) const;

// 3D
template <typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QueryRadius(const FVector& Center, float Radius, TArray<TElement>& OutResults, TPredicate&& Predicate = {}) const;

template <typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QueryRadiusMatching(const FVector& Center, float Radius, TConsumer&& Consumer, TPredicate&& Predicate = {}) const;

template <typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QueryAABB(const FBox& Box, TArray<TElement>& OutResults, TPredicate&& Predicate = {}) const;

template <typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QueryAABBMatching(const FBox& Box, TConsumer&& Consumer, TPredicate&& Predicate = {}) const;

template <typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QueryCylinder(const FVector& Center, float Radius, float HalfHeight, TArray<TElement>& OutResults, TPredicate&& Predicate = {}) const;

template <typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QueryCylinderMatching(const FVector& Center, float Radius, float HalfHeight, TConsumer&& Consumer, TPredicate&& Predicate = {}) const;

template <typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QueryCone(const FVector& Apex, const FVector& Direction, float Radius, float HalfAngleDegrees, TArray<TElement>& OutResults, TPredicate&& Predicate = {}) const;

template <typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QueryConeMatching(const FVector& Apex, const FVector& Direction, float Radius, float HalfAngleDegrees, TConsumer&& Consumer, TPredicate&& Predicate = {}) const;
```

| 参数 | 说明 |
| :--- | :--- |
| `Center` / `Apex` | 圆心 / 球心 / 圆柱中心 / 锥体顶点 |
| `ForwardDir` / `Direction` | 扇形基准朝向 / 锥体中轴朝向 |
| `Radius` | 查询半径或最大作用距离 |
| `HalfAngleDegrees` | 半角（度）；2D 扇形 `>= 180°` 退化为全圆 |
| `Box` | `FBox2D` 或 `FBox` 轴对齐包围盒 |
| `HalfHeight` | 圆柱垂直半高 |
| `OutResults` | 输出命中元素数组 |
| `Consumer` | 流式消费者，支持 `(Item)` 或 `(Item, Loc)` |
| `Predicate` | 过滤谓词，支持 `(const TElement&)` 或 `(const TElement&, const TVector&)`，可 `mutable`，每元素至多求值一次 |

- **查询语义**：2D 扇形零朝向或非正半角返回空；3D 锥体使用紧致球冠包围盒与零开方角判定，含 90° 法平面容差。
- **线程**：网格不做内部加锁，查询与写入不得并发。
- **失败语义**：非法查询参数返回空结果，不返回错误码。

### 近邻查询

```cpp
template <typename TPredicate = Game::Spatial::FAcceptAllPredicate>
[[nodiscard]] bool QueryNearest(const FVector2D& Center, float MaxSearchRadius, TElement& OutNearest, TPredicate&& Predicate = {}) const;

template <typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QueryKNearest(const FVector2D& Center, float MaxSearchRadius, int32 K, TArray<TElement>& OutNearestList, TPredicate&& Predicate = {}) const;

template <typename TConsumer, typename TPredicate = Game::Spatial::FAcceptAllPredicate>
void QueryKNearestMatching(const FVector2D& Center, float MaxSearchRadius, int32 K, TConsumer&& Consumer, TPredicate&& Predicate = {}) const;
```

- 3D 版本将 `FVector2D` 替换为 `FVector`，其余参数一致。
- `QueryNearest` 返回是否命中；未命中时返回 `false` 且不写入 `OutNearest`。
- `QueryKNearest*` 按距离由近至远升序输出；`K <= 16` 使用栈内联缓冲零堆分配，并按实际命中数预留输出数组。
- K 近邻流式消费者支持 `(const TElement&, double DistSq)`。

```cpp
#include "Grid/TSpatialHashGrid3D.h"

void FCombatTargeting::QuerySkillTargets(const TSpatialHashGrid3D<int32>& Grid, const FVector& CastPos, const FVector& AimDir) {
    Grid.QueryConeMatching(
        CastPos,
        AimDir,
        1200.0f,
        30.0f,
        [](int32 TargetId, const FVector& Loc) {
            ApplyConeDamage(TargetId, Loc);
        },
        [](int32 TargetId) {
            return IsHostileTarget(TargetId);
        }
    );

    TArray<int32> BounceTargets;
    Grid.QueryKNearest(CastPos, 2000.0f, 5, BounceTargets, [](int32 TargetId) {
        return IsAliveTarget(TargetId);
    });
}
```

## 世界级 Actor 分层索引 `UGameSpatialSubsystem`

```cpp
UCLASS()
class GAMESPATIAL_API UGameSpatialSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
    bool RegisterActor(AActor* Actor, FGameplayTag LayerTag = FGameplayTag());
    int32 RegisterActors(const TArray<AActor*>& Actors, FGameplayTag LayerTag = FGameplayTag());
    bool UnregisterActor(AActor* Actor);
    int32 UnregisterActors(const TArray<AActor*>& Actors);
    bool UpdateActorLocation(AActor* Actor, const FVector& NewLocation);
    bool UpdateActor(AActor* Actor);

    void QueryActorsInRadius(const FVector& Center, float Radius, TArray<AActor*>& OutActors, FGameplayTag LayerTag = FGameplayTag()) const;
    void QueryActorsInCone(const FVector& Apex, const FVector& Direction, float Radius, float HalfAngleDegrees, TArray<AActor*>& OutActors, FGameplayTag LayerTag = FGameplayTag()) const;
    void QueryActorsInCylinder(const FVector& Center, float Radius, float HalfHeight, TArray<AActor*>& OutActors, FGameplayTag LayerTag = FGameplayTag()) const;
    void QueryActorsInAABB(const FBox& Box, TArray<AActor*>& OutActors, FGameplayTag LayerTag = FGameplayTag()) const;
    bool QueryNearestActor(const FVector& Center, float MaxSearchRadius, AActor*& OutNearestActor, FGameplayTag LayerTag = FGameplayTag()) const;
    void QueryKNearestActors(const FVector& Center, float MaxSearchRadius, int32 K, TArray<AActor*>& OutActors, FGameplayTag LayerTag = FGameplayTag()) const;

    void ClearLayer(FGameplayTag LayerTag = FGameplayTag());
    void ClearAll();
    int32 PruneStaleActors(FGameplayTag LayerTag = FGameplayTag());
    int32 PruneAllStaleActors();
    int32 GetActorCount(FGameplayTag LayerTag = FGameplayTag()) const;
    int32 GetTotalActorCount() const;
    bool IsActorRegistered(const AActor* Actor) const;
    FGameplayTag GetActorLayer(const AActor* Actor) const;
    bool GetActorLocation(const AActor* Actor, FVector& OutLocation) const;
    void GetActorsInLayer(TArray<AActor*>& OutActors, FGameplayTag LayerTag = FGameplayTag()) const;
    [[nodiscard]] static FGameplayTag GetDefaultLayerTag() noexcept;
};

template <typename TConsumer>
void ForEachActorInRadius(const FVector& Center, float Radius, TConsumer&& Consumer, FGameplayTag LayerTag = FGameplayTag()) const;

template <typename TConsumer>
void ForEachActorInCone(const FVector& Apex, const FVector& Direction, float Radius, float HalfAngleDegrees, TConsumer&& Consumer, FGameplayTag LayerTag = FGameplayTag()) const;

template <typename TConsumer>
void ForEachActorInCylinder(const FVector& Center, float Radius, float HalfHeight, TConsumer&& Consumer, FGameplayTag LayerTag = FGameplayTag()) const;

template <typename TConsumer>
void ForEachActorInAABB(const FBox& Box, TConsumer&& Consumer, FGameplayTag LayerTag = FGameplayTag()) const;
```

| 接口组 | 行为 |
| :--- | :--- |
| `RegisterActor(s)` / `UnregisterActor(s)` | 同层已存在时原地更新位置（静止直接早退），跨层自动迁移；`UnregisterActor` 允许在 `EndPlay` 销毁期调用 |
| `UpdateActorLocation` / `UpdateActor` | 静止未变化时跳过图层网格查找直接早退；未注册、Actor 正在销毁或坐标非有限返回 `false` |
| `QueryActorsIn*` | 输出有效存活 Actor；`QueryNearestActor` 未命中时将 `OutNearestActor` 置 `nullptr` 并返回 `false` |
| `ForEachActorInRadius` / `Cone` / `Cylinder` / `AABB` | C++ 零堆分配流式原语，自动过滤已销毁或销毁中条目 |
| `ClearLayer` / `ClearAll` | `ClearLayer` 为 O(LayerSize)；`ClearAll` 清空全部图层 |
| `PruneStaleActors` / `PruneAllStaleActors` | 清理已回收或销毁的失效条目，返回清理数量 |
| `GetActorCount` / `GetTotalActorCount` | 含待惰性清理条目的索引数量 |
| `IsActorRegistered` / `GetActorLayer` / `GetActorLocation` / `GetActorsInLayer` | 存活且已注册才返回有效结果 |
| `GetDefaultLayerTag` | 返回 `Spatial.Layer.Default` |

- **线程**：非线程安全，所有方法必须在 GameThread 调用。
- **生命周期**：随 `UWorld` 创建与销毁；`Deinitialize` 清空全部图层与追踪表，不跨世界保留状态。
- **失败语义**：注册 / 更新 / 反查失败返回 `false` 或空数组；`GetActorLocation` 失败输出零向量。

```cpp
#include "Subsystem/UGameSpatialSubsystem.h"

void AEnemyActor::BeginPlay() {
    Super::BeginPlay();
    if (UGameSpatialSubsystem* Spatial = GetWorld()->GetSubsystem<UGameSpatialSubsystem>()) {
        Spatial->RegisterActor(this, EnemyLayerTag);
    }
}

void AEnemyActor::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    if (UGameSpatialSubsystem* Spatial = GetWorld()->GetSubsystem<UGameSpatialSubsystem>()) {
        Spatial->UnregisterActor(this);
    }
    Super::EndPlay(EndPlayReason);
}

void APlayerCharacter::PerformRadialPulse() {
    if (const UGameSpatialSubsystem* Spatial = GetWorld()->GetSubsystem<UGameSpatialSubsystem>()) {
        Spatial->ForEachActorInRadius(GetActorLocation(), 800.0f, [this](AActor* Enemy, const FVector& EnemyLoc) {
            ApplyPulseImpulse(Enemy, EnemyLoc);
        }, EnemyLayerTag);
    }
}
```

## 视锥与重要度心跳节流驱动器

### 上下文与档位配置

```cpp
struct FSpatialViewContext {
    FVector ViewLocation = FVector::ZeroVector;
    FVector ViewForward = FVector::ForwardVector;
    float FOVDegrees = 90.0f;
    float MaxDistance = 10000.0f;
};
```

| 字段 | 说明 |
| :--- | :--- |
| `ViewLocation` | 观察者世界坐标 |
| `ViewForward` | 前向朝向；零向量视为无效视锥 |
| `FOVDegrees` | 视锥全张角（度），有效范围 `(0, 360]` |
| `MaxDistance` | 最大视距，与 `DistanceFar` 取较小值生效 |

```cpp
struct FSpatialThrottleTierConfig {
    float DistanceNear = 1500.0f;
    float DistanceMid = 3000.0f;
    float DistanceFar = 6000.0f;
    float DistanceHysteresis = 50.0f;
    float NearInterval = 0.0f;
    float MidInterval = 0.0333f;
    float FarInterval = 0.0666f;
    float OutOfRangeOrBehindInterval = 0.5f;
    float HysteresisDegrees = 5.0f;
};
```

- 距离阈值单位厘米；`DistanceMid` 不得小于 `DistanceNear`。
- `NearInterval == 0.0f` 表示每帧满速；`HysteresisDegrees` 范围 `[0, 90]`。

### `FSpatialThrottleDriver`

```cpp
class GAMESPATIAL_API FSpatialThrottleDriver {
public:
    using FOwnerValidator = FThrottledTickScheduler::FOwnerValidator;
    using FThrottledTickCallback = FThrottledTickScheduler::FThrottledTickCallback;
    using FPositionGetter = TFunction<FVector()>;

    explicit FSpatialThrottleDriver(FThrottledTickScheduler& InScheduler);
    explicit FSpatialThrottleDriver(UThrottledTickerSubsystem& InSubsystem);
    ~FSpatialThrottleDriver();

    uint64 RegisterSpatialEntity(
        FOwnerValidator InValidator,
        FPositionGetter InPositionGetter,
        FThrottledTickCallback InCallback,
        bool bInHighSignificance = false
    );

    bool UnregisterSpatialEntity(uint64 InEntityHandle);
    bool SetEntitySignificance(uint64 InEntityHandle, bool bInHighSignificance);
    [[nodiscard]] bool IsEntityRegistered(uint64 InEntityHandle) const noexcept;

    void UpdateSpatialThrottle(const FSpatialViewContext& ViewContext);
    void SetTierConfig(const FSpatialThrottleTierConfig& InConfig);
    [[nodiscard]] const FSpatialThrottleTierConfig& GetTierConfig() const noexcept;
    [[nodiscard]] int32 Num() const noexcept;
    void Clear();
};
```

| 接口 | 参数 / 返回 | 行为 |
| :--- | :--- | :--- |
| 构造函数 | 调度器引用或节流子系统引用 | 绑定调度器时调用方须保证其生命周期长于驱动器；绑定子系统时用弱引用防悬垂 |
| `RegisterSpatialEntity` | 校验器、坐标获取器、Tick 回调、高重要度标记 | 初始以 `0.0f` 满帧间隔注册；返回句柄，`0` 表示失败 |
| `UnregisterSpatialEntity` | 句柄 | O(1) 移除并同步注销底层心跳，支持更新期重入 |
| `SetEntitySignificance` | 句柄、高重要度标记 | O(1)，下一次 `UpdateSpatialThrottle` 生效 |
| `IsEntityRegistered` | 句柄 | O(1) 查询活跃注册态 |
| `UpdateSpatialThrottle` | `FSpatialViewContext` | 每帧预计算四档阈值平方查表，集中评估距离 / 视锥 / 迟滞 / 重要度并下发新周期 |
| `SetTierConfig` / `GetTierConfig` | 配置 | 对非负与递增约束做安全钳制 |
| `Num` / `Clear` | 无 / 无 | 活跃实体数；清空支持回调内安全重入 |

- **线程**：非线程安全，所有方法必须在 GameThread 调用。
- **生命周期**：析构注销所有在调度器中注册的心跳；`UThrottledTickerSubsystem` 被弱引用跟踪。
- **失败语义**：句柄无效或未注册的注销 / 重要度切换返回 `false`；注册失败返回 `0`。

```cpp
#include "Async/UThrottledTickerSubsystem.h"
#include "Throttle/FSpatialThrottleDriver.h"

void UAiLodController::Initialize(UWorld* World) {
    UThrottledTickerSubsystem* Ticker = World->GetSubsystem<UThrottledTickerSubsystem>();
    ThrottleDriver = MakeUnique<FSpatialThrottleDriver>(*Ticker);

    FSpatialThrottleTierConfig Config;
    Config.DistanceNear = 1500.0f;
    Config.DistanceMid = 3500.0f;
    Config.DistanceFar = 7000.0f;
    Config.DistanceHysteresis = 80.0f;
    Config.HysteresisDegrees = 5.0f;
    ThrottleDriver->SetTierConfig(Config);
}
```

## 常见错误与边界

- **线程边界**：网格、子系统与节流驱动器均不做内部加锁，跨线程调用会破坏索引一致性。
- **坐标范围**：2D 单元格索引钳制在 `int32` 安全区间；3D 为 21 位有符号（约 `-1,048,576` ~ `1,048,575`），默认 `500cm` 边长覆盖约 ±5,242km；超出范围的有限坐标被钳制到边界格，`NaN` / `Inf` 返回 `6003`。
- **单元格边长**：构造时非有限或 `<= 1.0f` 的 `CellSize` 会静默回退 `500.0f`，不要依赖非法值报错。
- **近邻输出**：`QueryNearest` 未命中返回 `false`，不要读取 `OutNearest`；`QueryKNearest` 的 `K > 16` 不再享受零堆分配路径。
- **图层与销毁**：空 `LayerTag` 统一归入默认层；Actor 应在 `EndPlay` 主动反注册，长期运行可定期 `PruneAllStaleActors()`。
- **调度器寿命**：`FSpatialThrottleDriver` 绑定 `FThrottledTickScheduler&` 时，调度器必须先于驱动器存活；绑定子系统则无此约束。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [快速上手指南](../Guides/QuickStart.md)
