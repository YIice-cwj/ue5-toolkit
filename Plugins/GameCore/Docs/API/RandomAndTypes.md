# GameCore 确定性随机数、强类型结果与槽位容器 API 参考

本文档汇总 `GameCore` 提供的底层值语义类型与确定性随机数基础设施：
- **值类型与句柄容器 (`Types/`, `Containers/`)**：`TGameResult<T, E>`、`TGameHandle<TagType>`、`TSlotMap<T, TagType>`、`TCancelableScopeGuard`、`FGameDistId`
- **确定性随机数与防脸黑算法 (`Random/`)**：`FGameRandomStream`、`FPseudoRandomDistribution`、`TShuffleBag<T>`、`TWeightedPicker<T>`、`URandomSubsystem`

> **组件解耦与迁移提示**：
> - **空间哈希索引**（`TSpatialHashGrid2D` / `TSpatialHashGrid3D`）：已解耦迁移至独立基础设施插件 `Plugins/GameSpatial`。

---

## 1. 强类型错误处理 `TGameResult<TSuccess, TError>`

头文件：`#include "Types/TGameResult.h"`

替代出参布尔与异常，显式表达可能失败的纯函数与加载/校验契约（支持 `void` 成功特化 `TGameResult<void, E>`）：

- **工厂构建**：`TGameResult::Success(Value)`、`TGameResult::Failure(Error)`。
- **值访问与解包**：`IsSuccess()`、`IsFailure()`、`GetValue()`、`GetValueOr(Fallback)`、`ExtractValue()`。
- **Monadic 链式算子 (C++23 风格函数式管线)**：
  - `.Map(Func)`：成功时对值进行映射转换（支持映射为普通值或 `void`）；失败时原样透传。
  - `.AndThen(Func)`：成功时调用返回新 `TGameResult` 的闭包进行扁平化绑定（FlatMap）；失败时短路阻断并透传错误。
  - `.MapError(Func)`：失败时转换/丰富错误对象；成功时原样透传。
  - `.OrElse(Func)`：失败时调用恢复闭包，允许返回兜底成功值或替换错误；成功时原样透传。
  - *重载说明*：所有算子均提供 `const&` 与 `&&` 右值引用重载，配合 `ExtractValue()`/`ExtractError()` 实现仅移动类型（如 `TUniquePtr`）的零拷贝流水线。
- **链式错误向上传播宏**：`GAME_TRY(ResultExpr)` 与 `GAME_TRY_CTX(ResultExpr, ContextText)`。

```cpp
enum class EParseError : uint8 { EmptyInput, OutOfRange };

TGameResult<int32, EParseError> ParseLevel(const FString& Raw) {
    if (Raw.IsEmpty()) {
        return TGameResult<int32, EParseError>::Failure(EParseError::EmptyInput);
    }
    return TGameResult<int32, EParseError>::Success(FCString::Atoi(*Raw));
}

// 1. 函数式 Monadic 链式处理流水线
const FString DisplayMessage = ParseLevel(TEXT("10"))
    .Map([](int32 Val) { return Val * 10; })
    .AndThen([](int32 Multiplied) -> TGameResult<FString, EParseError> {
        if (Multiplied > 50) {
            return TGameResult<FString, EParseError>::Success(FString::Printf(TEXT("High: %d"), Multiplied));
        }
        return TGameResult<FString, EParseError>::Failure(EParseError::OutOfRange);
    })
    .OrElse([](EParseError Err) {
        return TGameResult<FString, EParseError>::Success(TEXT("Default: 0"));
    })
    .GetValue();

// 2. 错误处理与安全解包
TGameResult<int32, EParseError> Result = ParseLevel(TEXT("12"));
const int32 Level = Result.GetValueOr(1);

// 3. 使用宏进行跨函数错误自动传播
TGameResult<int32, EParseError> ProcessPlayerLevel(const FString& Raw) {
    auto Result = ParseLevel(Raw);
    GAME_TRY(Result);
    const int32 ParsedLevel = Result.ExtractValue();
    return TGameResult<int32, EParseError>::Success(ParsedLevel * 2);
}
```

---

## 2. 代际安全句柄 `TGameHandle` 与紧凑槽位表 `TSlotMap`

头文件：
- `#include "Types/TGameHandle.h"`
- `#include "Containers/TSlotMap.h"`

- `TGameHandle<TagType>`：64 位值类型句柄（高 32 位代际号 `Generation`，低 32 位槽位索引 `Index`），天然免疫 ABA 悬垂复用。
- `TSlotMap<TElement, TagType>`：基于稠密数据数组 + 稀疏槽位跳转表的紧凑容器，插入、删除与句柄查找均为 $O(1)$，且支持缓存友好的密集数组切片（`GetDenseValues()` / `GetMutableDenseValues()`）与句柄关联遍历（`ForEach` / `GetHandleForDenseIndex`）。

```cpp
struct FBuffSlotTag {};
using FBuffHandle = TGameHandle<FBuffSlotTag>;

TSlotMap<float, FBuffSlotTag> ActiveBuffTimers;
const FBuffHandle Handle = ActiveBuffTimers.Insert(5.0f);

if (float* Remaining = ActiveBuffTimers.Find(Handle)) {
    *Remaining -= 0.1f;
}

// 遍历密集数组
ActiveBuffTimers.ForEach([](FBuffHandle CurHandle, float& RemainingTime) {
    RemainingTime -= 0.016f;
});

ActiveBuffTimers.Remove(Handle); // 旧 Handle 代际立即失效，Find 返回 nullptr
```

---

## 3. 确定性随机流 `FGameRandomStream` 与 `URandomSubsystem`

头文件：
- `#include "Random/FGameRandomStream.h"`
- `#include "Random/URandomSubsystem.h"`

### 核心特性
- **跨平台位级一致**：采用纯整数算术 **PCG32 (Permuted Congruential Generator)** + SplitMix64 种子雪崩散列 + Lemire 无偏区间映射，彻底消除标准库实现差异。
- **通道隔离与状态快照**：支持 `GetState()` / `SetState()` 帧级回滚快照与二进制序列化（联机重放、Roguelike 存档复现）。
- **世界级命名流隔离 (`URandomSubsystem`)**：以 `UWorldSubsystem` 管理主种子与按 `FName` 懒派生的业务随机流（如 `"Combat.Crit"`、`"Loot.Drop"`），避免 UI 粒子随机消耗污染战斗判定序列。

```cpp
URandomSubsystem& RngSys = URandomSubsystem::Get(GetWorld());
RngSys.InitializeStream(TEXT("Combat.Crit"), 20261106ULL);

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
| `TWeightedPicker<T>` | 大型掉落表、高频 AI 行为权重轮盘选择 | Vose's Alias Method（沃斯别名法），$O(N)$ 预构建/惰性构建别名表，单次采样严格恒定时间 $O(1)$ |

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

## 7. 结构化错误契约 `FGameError` 与 `TGameStatusResult<T>`

头文件：
- `#include "Types/FGameError.h"`
- `#include "Types/TGameResult.h"`

### 核心特性
- **跨模块统一错误模型**：统一包含 `ModuleId`、`ErrorCode`、`Severity`（`EGameErrorSeverity`）与人类可读的 `Message`；
- **蓝图反射支持**：`USTRUCT(BlueprintType)`，可无缝暴露至蓝图与外部数据流；
- **状态结果特化**：`TGameStatusResult<T>` 为 `TGameResult<T, FGameError>` 的官方推荐别名；
- **链式上下文追踪**：通过 `WithContext(...)` 或 `GAME_TRY_CTX(Result, ContextText)` 在调用栈向上回溯时逐层追加上下文。

```cpp
FGameError Err = FGameError::Make(
    Game::ModuleId::Asset,
    Game::CommonErrorCodes::NotFound,
    TEXT("Texture_01 missing"),
    EGameErrorSeverity::Error
);
// 追加调用层级说明
FGameError Enriched = Err.WithContext(TEXT("LoadCharacterMesh"));
```

---

## 8. 唯一 ID 原语体系（分布式雪花 ID 与极速本地 ID）

头文件：
- `#include "Types/FGameDistId.h"`
- `#include "Types/FGameDistIdGenerator.h"`
- `#include "Types/FGameLocalId.h"`
- `#include "Types/FGameLocalIdGenerator.h"`

### 体系分工与核心能力
- **`FGameDistId`（分布式雪花 ID）**：专司多服网络、跨节点协同与持久化记录。采用 `[1-bit 保留] + [41-bit 毫秒时间戳] + [10-bit 节点编号] + [12-bit 递增序号]` 紧凑 64 位编码，时钟停滞/回拨自旋并自动推进逻辑毫秒防死锁；
- **`FGameLocalId`（极速本地自增 ID）**：专司进程内、单机与客户端运行时会话。内部采用松弛内存序原子 `fetch_add`，零锁零等待，每秒可产数亿个，零时间戳/节点开销；
- **雪崩散列（Avalanche Hash）**：两者均采用 MurmurHash3 64-bit Finalizer 散列算法重载 `GetTypeHash`，消灭紧凑自增序列与邻近雪花序列在 `TMap` / `TSet` 中的哈希聚集；
- **双向序列化与解析**：支持十进制与 `0x` 前缀十六进制（`ToString()`、`ToHex()`、`FromString()`），原生支持引擎 `LexToString` 与 `LexFromString`；
- **零成本双向互转**：支持 `LocalId.ToDistId()` 与 `FGameLocalId::FromDistId(DistId)` 互相映射。

```cpp
// 1. 分布式雪花 ID
FGameDistId DistId = FGameDistIdGenerator::Get().NextId();
FString DistStr = DistId.ToString();
FGameDistId ParsedDist;
bool bParsed = FGameDistId::FromString(DistStr, ParsedDist);

// 2. 本地极速自增 ID
FGameLocalId LocalId = FGameLocalIdGenerator::NextId();
FString LocalHex = LocalId.ToHex(); // 如 "0x0000000000000001"

// 3. 体系互转与容器无缝支持
TMap<FGameLocalId, FString> EntityMap;
EntityMap.Add(LocalId, TEXT("PlayerActor"));
```

---

## 9. 强类型枚举紧凑位掩码 `TBitMask<TEnum>`

头文件：`#include "Types/TBitMask.h"`

### 职责与场景
- 基于强类型 `enum class` 的紧凑位集合模板，底层存储紧凑对齐枚举底层类型（`uint8` ~ `uint64`）；
- 内部针对负数与超过位宽（`>= BIT_WIDTH`）的枚举位提供编译期与运行期安全拦截，杜绝未定义移位行为（UB）；
- 重载了完整按位操作符（`|`, `&`, `^`, `~`）与语义谓词（`Has`, `HasAny`, `HasAll`, `HasNone`, `IsEmpty`）；
- 支持 `GetTypeHash`，可直接作为 `TMap` / `TSet` 的键。

```cpp
enum class EBuffChannel : uint8 {
    Combat = 0,
    Movement = 1,
    Passive = 2
};

TBitMask<EBuffChannel> Mask(EBuffChannel::Combat);
Mask.Set(EBuffChannel::Movement);
if (Mask.Has(EBuffChannel::Combat)) {
    // 包含战斗通道
}
```
