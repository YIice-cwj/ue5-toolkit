# GameCore 确定性随机数、强类型结果与槽位容器 API 参考

本文档汇总 `GameCore` 提供的底层值语义类型与确定性随机数基础设施：
- **值类型与句柄容器 (`Types/`)**：`TGameResult<T, E>`、`TGameHandle<TagType>`、`TSlotMap<T, TagType>`
- **确定性随机数与防脸黑算法 (`Random/`)**：`FGameRandomStream`、`FPseudoRandomDistribution`、`TShuffleBag<T>`、`TWeightedPicker<T>`、`URandomSubsystem`

---

## 1. 强类型错误处理 `TGameResult<TValue, TError>`

头文件：`#include "Types/TGameResult.h"`

替代出参布尔与异常，显式表达可能失败的纯函数与加载/校验契约（支持 `void` 成功特化 `TGameResult<void, E>`）：

```cpp
enum class EParseError : uint8 { EmptyInput, OutOfRange };

TGameResult<int32, EParseError> ParseLevel(const FString& Raw) {
    if (Raw.IsEmpty()) {
        return TGameResult<int32, EParseError>::Err(EParseError::EmptyInput);
    }
    return TGameResult<int32, EParseError>::Ok(FCString::Atoi(*Raw));
}

// 链式组合：Map / MapError / AndThen / ValueOr
const int32 Clamped = ParseLevel(TEXT("12"))
    .Map([](int32 Lv) { return Lv * 10; })
    .ValueOr(10);
```

---

## 2. 代际安全句柄 `TGameHandle` 与紧凑槽位表 `TSlotMap`

头文件：
- `#include "Types/TGameHandle.h"`
- `#include "Types/TSlotMap.h"`

- `TGameHandle<TagType>`：64 位值类型句柄（高 32 位代际号 `Generation`，低 32 位槽位索引 `Index`），天然免疫 ABA 悬垂复用。
- `TSlotMap<TElement, TagType>`：基于稠密数据数组 + 稀疏槽位跳转表的紧凑容器，插入、删除与句柄查找均为 $O(1)$，且支持零间接跳转缓存友好顺序遍历（`GetDenseValues()` / `ForEach`）。

```cpp
struct FBuffSlotTag {};
using FBuffHandle = TGameHandle<FBuffSlotTag>;

TSlotMap<float, FBuffSlotTag> ActiveBuffTimers;
const FBuffHandle Handle = ActiveBuffTimers.Insert(5.0f);

if (float* Remaining = ActiveBuffTimers.Find(Handle)) {
    *Remaining -= 0.1f;
}
ActiveBuffTimers.Remove(Handle); // 旧 Handle 代际立即失效，Find 返回 nullptr
```

---

## 3. 确定性随机流 `FGameRandomStream` 与 `URandomSubsystem`

头文件：
- `#include "Random/FGameRandomStream.h"`
- `#include "Random/URandomSubsystem.h"`

### 核心特性
- **跨平台位级一致**：采用纯整数算术 **PCG32 (Permuted Congruential Generator)** + Lemire 无偏区间映射 + Box-Muller 标准正态分布，彻底消除标准库实现差异。
- **确定性派生与状态快照**：支持 `Fork(Salt)` 派生独立子流（互不推进父流游标）及 `SaveState()` / `RestoreState()` 帧级回滚快照（联机重放、Roguelike 种子复现）。
- **世界级命名流隔离 (`URandomSubsystem`)**：以 `UWorldSubsystem` 管理主种子与按 `FName` 懒派生的业务随机流（如 `"Combat.Crit"`、`"Loot.Drop"`），避免 UI 粒子随机消耗污染战斗判定序列。

```cpp
URandomSubsystem& RngSys = URandomSubsystem::Get(GetWorld());
RngSys.InitializeMasterSeed(20261106ULL);

FGameRandomStream& CombatStream = RngSys.GetOrCreateStream(TEXT("Combat.Crit"));
const bool bCrit = CombatStream.RollChance(0.25f);
const int32 DamageRoll = CombatStream.RandRange(95, 105);
```

---

## 4. 防脸黑伪随机与抽卡容器

头文件：
- `#include "Random/FPseudoRandomDistribution.h"`
- `#include "Random/TShuffleBag.h"`
- `#include "Random/TWeightedPicker.h"`

| 组件 | 适用场景 | 算法与复杂度 |
| :--- | :--- | :--- |
| `FPseudoRandomDistribution` | 暴击、闪避、被动触发防连黑/连爆 | 二分反解马尔可夫链稳态期望概率常数 $C$，每次未命中线性累加 $N \times C$，命中即归位 |
| `TShuffleBag<T>` | 音效轮播、保底卡池、固定配比掉落 | 不放回抽样袋，抽空一轮后自动使用传入 `FGameRandomStream` 执行 Fisher-Yates 洗牌 |
| `TWeightedPicker<T>` | 大型掉落表、AI 行为权重选择 | 离散前缀和 + 二分查找（`Algo::UpperBound`），单次采样 $O(\log N)$ |

---

## 5. 可取消作用域事务守卫 `TCancelableScopeGuard`

头文件：`#include "Types/TCancelableScopeGuard.h"`

### 职责与原理
- 零堆分配、纯值语义 RAII 事务守卫；
- 离开当前作用域时自动执行回滚/清理闭包；若业务顺利完成，可调用 `.Dismiss()` 取消执行；
- 相比引擎 `ON_SCOPE_EXIT`，原生支持动态取消与移动语义转移（如转移进函数返回值或长生命周期对象）。

```cpp
bool PerformComplexTransaction() {
    AcquireResourceA();
    auto Rollback = MakeCancelableScopeGuard([]() {
        ReleaseResourceA();
    });

    if (!StepTwo()) {
        return false; // 提前退出，Rollback 析构自动执行 ReleaseResourceA()
    }

    Rollback.Dismiss(); // 事务全流程成功，取消自动回滚
    return true;
}
```

---

## 6. 64 位分布式单调递增雪花唯一 ID `FGameDistId` 与 `FGameDistIdGenerator`

头文件：`#include "Types/FGameDistId.h"`

### 布局与特性
- **64 位紧凑结构**：`[1-bit 保留位] + [41-bit 相对毫秒时间戳] + [10-bit 节点编号] + [12-bit 递增序号]`；
- **高性能无锁单调自增**：支持同一毫秒内多达 4096 个唯一 ID 生成，跨网络和跨关卡无碰撞；
- **时间与节点自解**：无需查表，通过位运算直接解析生成时间（`GetTimestampMilliseconds`）与生成节点（`GetNodeId`）；
- **原生序列化**：支持 `operator<<` 与 `GetTypeHash`，可直接作为 `TMap` / `TSet` 的键，极低带宽同步。

```cpp
// 通过全局生成器获取下一个唯一 ID
const FGameDistId EntityId = FGameDistIdGenerator::Get().NextId();

// 反解时间戳与节点
const int64 CreatedTimeMs = EntityId.GetTimestampMilliseconds(FGameDistIdGenerator::DEFAULT_CUSTOM_EPOCH);
const uint16 NodeId = EntityId.GetNodeId();
```

---

## 7. 纯逻辑空间哈希网格 `TSpatialHashGrid2D`

头文件：`#include "Spatial/TSpatialHashGrid2D.h"`

### 职责与原理
- 纯内存数学空间索引，完全脱离 PhysX / Chaos 物理管线与 `UPrimitiveComponent`；
- 空间均匀网格分桶，支持同单元格内坐标微移零开销（跨网格自动换桶并释放空桶）；
- 纳秒级提供半径球形/圆形范围检索（`QueryRadius`）、AABB 矩形框检索（`QueryAABB`）与最近目标查找（`QueryNearest`）。

```cpp
TSpatialHashGrid2D<FBuffHandle> EnemyGrid(400.0f); // 400 厘米网格单元

// 插入与移动更新
EnemyGrid.Insert(MonsterHandle, FVector2D(1200.0, 800.0));
EnemyGrid.Update(MonsterHandle, FVector2D(1250.0, 820.0));

// 技能索敌：半径 500 厘米范围检索
TArray<FBuffHandle> NearbyEnemies;
EnemyGrid.QueryRadius(PlayerPos2D, 500.0f, NearbyEnemies);

// 查找最近敌人
FBuffHandle NearestTarget;
if (EnemyGrid.QueryNearest(PlayerPos2D, 1000.0f, NearestTarget)) {
    // 锁定目标...
}
```
