# GameCore 确定性随机数 API 参考

> **一句话**：确定性随机流、多通道管理、PRD 平滑分布、洗牌袋与 O(1) 权重抽取的公共 API。
> **头文件**：`Random/FGameRandomStream.h`、`Random/FGameRandomManager.h`、`Random/FPseudoRandomDistribution.h`、`Random/TShuffleBag.h`、`Random/TWeightedPicker.h`、`Random/URandomSubsystem.h` · **线程**：各随机流、分布与容器均非线程安全；`FGameRandomManager` 与 `URandomSubsystem` 不提供内部锁 · **失败语义**：`ImportStreams` 校验失败原子回滚并返回 `false`；空容器 `Draw` / `Pick` 触发断言，安全路径用 `TryDraw` / `TryPick`

## 30 秒速览

- **随机流**：`FGameRandomStream` 采用 PCG32 + SplitMix64，整数区间使用 Lemire 无偏映射；`GetState` / `SetState` 支持快照回滚。
- **通道隔离**：`FGameRandomManager` 按 `FName` 通道隔离流，导出前按字典序排序；导入校验魔数 `0x47524E47`（`GRNG`）、版本 1 与 4096 通道上限。
- **PRD 与抽取容器**：`FPseudoRandomDistribution` 以标称概率反解步长常数 C；`TShuffleBag<T>` 不放回 O(1) 抽取并自动重洗；`TWeightedPicker<T>` 以 Vose's Alias Method 做 O(N) 构建、O(1) 抽取。
- **引擎外壳**：`URandomSubsystem` 是 `UWorldSubsystem` 薄壳，全部委托 `FGameRandomManager`。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FGameRandomStream::RandRange` | 闭区间整数随机 | 非线程安全；`Min > Max` 自动交换 |
| `FGameRandomStream::FRand` | 生成 `[0.0f, 1.0f)` 浮点随机 | 非线程安全；多线程共享破坏确定性 |
| `FGameRandomStream::RollChance` | 按概率判定布尔结果 | `Probability <= 0` 恒 `false`，`>= 1` 恒 `true` |
| `FGameRandomStream::GetState` / `SetState` | 状态快照与精确恢复 | `SetState` 不做二次种子混合 |
| `FGameRandomManager::GetOrCreateStream` | 惰性创建并返回通道流裸引用 | 非线程安全；移除通道后裸引用失效 |
| `FGameRandomManager::ExportStreams` / `ImportStreams` | 导出或原子导入全部流状态 | 导入校验魔数、版本与 4096 通道上限，失败回滚 |
| `FPseudoRandomDistribution::Roll` | PRD 平滑概率判定 | 命中清零 `FailureStreak`，未命中递增 |
| `TShuffleBag<T>::Draw` | 不放回 O(1) 抽取 | 空袋触发 `checkf`；安全路径用 `TryDraw` |
| `TWeightedPicker<T>::Pick` | O(1) 权重抽取 | 首次调用惰性构建；空表触发 `checkf`，安全路径用 `TryPick` |
| `URandomSubsystem::Get` | 取当前 `UWorld` 的随机子系统 | 无有效 `UWorld` 触发 `check` 断言，不确定时用 `GetPtr` |

## 随机流 `FGameRandomStream`

头文件：`#include "Random/FGameRandomStream.h"`

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FGameRandomStream {
    GENERATED_BODY()

public:
    static constexpr uint64 DEFAULT_SEED = 0x853C49E6748FEA9BULL;
    static constexpr uint64 PCG_MULTIPLIER = 6364136223846793005ULL;
    static constexpr uint64 PCG_INCREMENT = 1442695040888963407ULL;

    FGameRandomStream() noexcept;
    explicit FGameRandomStream(uint64 InSeed) noexcept;

    void Initialize(uint64 InSeed) noexcept;

    [[nodiscard]] uint32 NextUInt32() noexcept;
    [[nodiscard]] uint32 NextBoundedUInt32(uint32 Bound) noexcept;
    [[nodiscard]] int32 RandRange(int32 Min, int32 Max) noexcept;
    [[nodiscard]] float FRand() noexcept;
    [[nodiscard]] float FRandRange(float Min, float Max) noexcept;
    [[nodiscard]] bool RollChance(float Probability) noexcept;
    [[nodiscard]] FVector RandomUnitVector() noexcept;
    [[nodiscard]] FVector RandomVectorInConeRad(const FVector& ConeDir, float ConeHalfAngleRad) noexcept;

    [[nodiscard]] uint64 GetState() const noexcept;
    void SetState(uint64 InState) noexcept;
    bool operator==(const FGameRandomStream& Other) const noexcept;
    void SerializeState(FArchive& Ar);
};
```

<details>
<summary>展开方法参数与区间语义</summary>

| 接口 | 参数 | 返回 / 区间 |
| :--- | :--- | :--- |
| `FGameRandomStream(Seed)` | 64 位种子 | 经 SplitMix64 混合后初始化状态 |
| `Initialize` | 64 位种子 | 重新混合并覆盖当前状态 |
| `NextUInt32` | 无 | 32 位均匀无符号整数 |
| `NextBoundedUInt32` | `Bound` | `[0, Bound)`；`Bound == 0` 返回 `0` |
| `RandRange` | `Min`, `Max` | 闭区间 `[Min, Max]`；`Min > Max` 自动交换 |
| `FRand` | 无 | `[0.0f, 1.0f)` |
| `FRandRange` | `Min`, `Max` | `[Min, Max]`；`Min > Max` 自动交换 |
| `RollChance` | `Probability` | `<= 0` 恒 `false`，`>= 1` 恒 `true` |
| `RandomUnitVector` | 无 | 单位球面立体角均匀采样的单位向量 |
| `RandomVectorInConeRad` | `ConeDir`, 半张角弧度 | 圆锥内单位向量；零向量返回零向量，半张角钳制到 `[0, PI]` |
| `GetState` / `SetState` | 64 位状态 | 快照与精确恢复，`SetState` 不做二次种子混合 |
| `SerializeState` / `operator<<` | `FArchive&` | 写入或读取 64 位状态 |

</details>

- **线程**：非线程安全；多线程共享同一流会破坏确定性，需外部加锁或按线程分流。
- **确定性**：PCG32（PCG-XSH-RR，64 位内部状态、32 位输出）为纯整数同余推进，SplitMix64 以黄金分割比常数做双重雪崩混合，跨 Windows / Mac / Linux / 移动平台位级一致。
- **生命周期**：状态可随存档或网络包序列化；`GetState` / `SetState` 用于帧级回滚与回放对齐。
- **失败语义**：随机接口本身无失败返回；非法区间通过交换或钳制处理，不会触发 UB。

```cpp
FGameRandomStream Rng(12345678ULL);

const int32 DiceRoll = Rng.RandRange(1, 6);
const float NormalizedRate = Rng.FRand();
const bool bTriggered = Rng.RollChance(0.35f);

const FVector ShotDir = Rng.RandomVectorInConeRad(
    FVector::ForwardVector,
    FMath::DegreesToRadians(5.0f)
);

const uint64 SavedState = Rng.GetState();
Rng.RandRange(1, 100);
Rng.SetState(SavedState);
```

## 多通道管理 `FGameRandomManager`

头文件：`#include "Random/FGameRandomManager.h"`

```cpp
class GAMECORE_API FGameRandomManager {
public:
    static constexpr uint32 ARCHIVE_MAGIC = 0x47524E47u;
    static constexpr uint32 ARCHIVE_VERSION = 1u;
    static constexpr int32 MAX_IMPORT_STREAMS = 4096;

    FGameRandomManager() = default;
    ~FGameRandomManager() = default;

    [[nodiscard]] FGameRandomStream& GetOrCreateStream(const FName& ChannelName);
    [[nodiscard]] TSharedRef<FGameRandomStream> GetOrCreateStreamShared(const FName& ChannelName);
    FGameRandomStream& InitializeStream(const FName& ChannelName, uint64 Seed);

    [[nodiscard]] bool HasStream(const FName& ChannelName) const;
    bool RemoveStream(const FName& ChannelName);
    void ClearAllStreams();

    [[nodiscard]] int32 GetStreamCount() const noexcept;
    int32 GetChannelNames(TArray<FName>& OutChannelNames) const;

    void ExportStreams(FArchive& Ar) const;
    bool ImportStreams(FArchive& Ar);

    [[nodiscard]] static FName NormalizeChannelName(const FName& ChannelName) noexcept;
    [[nodiscard]] static uint64 DeriveChannelDefaultSeed(const FName& NormalizedName);
};
```

| 接口 | 说明 |
| :--- | :--- |
| `GetOrCreateStream` | 惰性创建并返回裸引用；`NAME_None` 归入 `Default` 通道 |
| `GetOrCreateStreamShared` | 返回 `TSharedRef`，通道被移除后仍保证内存安全 |
| `InitializeStream` | 用显式种子原地重置通道，既有引用不悬垂 |
| `RemoveStream` / `ClearAllStreams` | 移除通道；此前 `GetOrCreateStream` 的裸引用随之失效 |
| `ExportStreams` | 按通道名字典序导出全部流状态 |
| `ImportStreams` | 校验魔数、版本、通道上限并原子恢复；失败时回滚 |

- **线程**：非线程安全，无内部锁；跨线程使用需外部同步。
- **零引擎依赖**：纯 C++ 核心，零 `UWorld` 依赖，可在独立工具、子线程或纯单元测试中实例化。
- **地址稳定性**：内部以 `TSharedRef<FGameRandomStream>` 存储，新增通道导致 `TMap` 扩容时既有随机流对象地址不变。
- **生命周期**：裸引用只在通道存续期间有效；跨移除周期请持有 `GetOrCreateStreamShared` 返回值。
- **失败语义**：`ImportStreams` 在魔数、版本、重复通道或格式非法时返回 `false`，不破坏既有状态。

```cpp
FGameRandomManager Manager;

FGameRandomStream& CombatRng = Manager.GetOrCreateStream(TEXT("Combat"));
FGameRandomStream& LootRng = Manager.GetOrCreateStream(TEXT("Loot"));
Manager.InitializeStream(TEXT("Loot"), 9999ULL);

FBufferArchive Ar;
Manager.ExportStreams(Ar);

FMemoryReader Reader(Ar);
FGameRandomManager RestoredManager;
const bool bImportSuccess = RestoredManager.ImportStreams(Reader);
```

## PRD 平滑分布 `FPseudoRandomDistribution`

头文件：`#include "Random/FPseudoRandomDistribution.h"`

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FPseudoRandomDistribution {
    GENERATED_BODY()

public:
    static constexpr int32 MAX_FAILURE_STREAK = 1000000;

    FPseudoRandomDistribution() = default;
    explicit FPseudoRandomDistribution(float InNominalProbability) noexcept;

    void SetNominalProbability(float InNominalProbability) noexcept;
    [[nodiscard]] float GetNominalProbability() const noexcept;
    [[nodiscard]] float GetIncrementConstant() const noexcept;
    [[nodiscard]] int32 GetFailureStreak() const noexcept;
    [[nodiscard]] float GetNextRollProbability() const noexcept;
    [[nodiscard]] bool Roll(FGameRandomStream& Stream) noexcept;
    void Reset() noexcept;

    [[nodiscard]] static float CalculateConstantForProbability(float Probability) noexcept;
    [[nodiscard]] static double CalculatePreciseConstantForProbability(double Probability) noexcept;
};
```

| 接口 | 说明 |
| :--- | :--- |
| 构造 / `SetNominalProbability` | 非有限值按 `0` 处理，其余钳制到 `[0.0f, 1.0f]`，并重算步长常数 C |
| `GetNextRollProbability` | 预览下一次判定概率 `(N + 1) × C`，上限 1.0 |
| `Roll(Stream)` | 命中返回 `true` 并清零 `FailureStreak`；未命中返回 `false` 并递增 |
| `GetFailureStreak` / `Reset` | 查询连续未命中次数 / 仅清零计数，保留概率与常数 C |
| `CalculateConstantForProbability` | 双精度二分 60 轮逼近，返回单精度 C |
| `CalculatePreciseConstantForProbability` | 同算法返回双精度 C，降低极小概率截断误差 |

- **线程**：状态机非线程安全，`FailureStreak` 为实例内可变状态。
- **生命周期**：状态可随 `USTRUCT` 存档；`Reset()` 不改变标称概率。
- **数学契约**：长期宏观触发频率收敛到标称概率 P，同时抑制连续未命中与连续命中的极端序列。
- **边界**：`MAX_FAILURE_STREAK = 1000000` 为连续未命中计数上限；概率为 0 或 1 时退化为恒定结果。

```cpp
FPseudoRandomDistribution CritPRD(0.20f);
FGameRandomStream& CombatRng = Manager.GetOrCreateStream(TEXT("Combat"));

for (int32 Hit = 0; Hit < 10; ++Hit) {
    if (CritPRD.Roll(CombatRng)) {
        // 触发暴击，内部清零连续未命中计数
    }
}
```

## 不放回洗牌袋 `TShuffleBag<T>`

头文件：`#include "Random/TShuffleBag.h"`

```cpp
template <typename TValue>
class TShuffleBag {
public:
    using ValueType = TValue;
    using SizeType = int32;

    void Reserve(SizeType Capacity);
    void Add(const ValueType& Item, SizeType Count = 1);
    void Add(ValueType&& Item);

    template <typename... ArgsType>
    void Emplace(ArgsType&&... Args);

    [[nodiscard]] ValueType Draw(FGameRandomStream& Stream);
    [[nodiscard]] const ValueType* TryDraw(FGameRandomStream& Stream);

    void Reshuffle() noexcept;
    void Clear();

    [[nodiscard]] SizeType Num() const noexcept;
    [[nodiscard]] SizeType Remaining() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] const TArray<ValueType>& GetItems() const noexcept;
};
```

- **抽取算法**：随机选中未抽出区间的一项，与区间末尾交换并前移游标，单次 O(1)。
- **自动重洗**：`Remaining() == 0` 时下一次 `TryDraw` 自动重置游标，无需重新分配。
- **动态并入**：抽取过程中 `Add` / `Emplace` 的新元素立即进入当前可抽池，不回退已抽历史。
- **失败语义**：空袋 `Draw` 触发 `checkf`，`TryDraw` 返回 `nullptr`。
- **指针有效期**：`TryDraw` 返回内部元素指针，下一次抽取交换后可能失效，禁止跨次持有；需要稳定值时用 `Draw` 的值拷贝。

```cpp
TShuffleBag<FString> CardBag;
CardBag.Add(TEXT("Common"), 70);
CardBag.Add(TEXT("Rare"), 25);
CardBag.Add(TEXT("Epic"), 5);

FGameRandomStream& GachaRng = Manager.GetOrCreateStream(TEXT("Gacha"));
const FString PulledCard = CardBag.Draw(GachaRng);
```

## 权重抽取 `TWeightedPicker<T>`

头文件：`#include "Random/TWeightedPicker.h"`

```cpp
template <typename TValue>
class TWeightedPicker {
public:
    using ValueType = TValue;
    using SizeType = int32;

    void Reserve(SizeType Capacity);
    bool AddEntry(ValueType Item, double Weight);
    void Build() const;
    [[nodiscard]] const ValueType* TryPick(FGameRandomStream& Stream) const;
    [[nodiscard]] const ValueType& Pick(FGameRandomStream& Stream) const;

    void Clear();
    [[nodiscard]] SizeType Num() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] double GetTotalWeight() const noexcept;
    [[nodiscard]] double GetWeight(SizeType Index) const noexcept;
};
```

- **算法**：Vose's Alias Method 在 O(N) 构建概率表与别名表后，单次抽取为 O(1)，与条目数量无关。
- **构建时机**：`Build()` 可显式预构建；`TryPick` / `Pick` 在首次调用时惰性构建。
- **权重校验**：`AddEntry` 拒绝非有限值、`<= 0` 或总和溢出的权重，返回 `false`。
- **失败语义**：空表 `Pick` 触发 `checkf`；`TryPick` 返回 `nullptr`。
- **失效规则**：`AddEntry` / `Clear` 会使别名表失效，下次抽取时重建。

```cpp
TWeightedPicker<FName> EnemySpawnTable;
EnemySpawnTable.AddEntry(TEXT("Goblin"), 60.0);
EnemySpawnTable.AddEntry(TEXT("Orc"), 30.0);
EnemySpawnTable.AddEntry(TEXT("Dragon"), 10.0);
EnemySpawnTable.Build();

FGameRandomStream& SpawnRng = Manager.GetOrCreateStream(TEXT("Spawn"));
const FName ChosenEnemy = EnemySpawnTable.Pick(SpawnRng);
```

## 世界子系统 `URandomSubsystem`

头文件：`#include "Random/URandomSubsystem.h"`

```cpp
UCLASS()
class GAMECORE_API URandomSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
    void Deinitialize() override;

    [[nodiscard]] FGameRandomStream& GetOrCreateStream(const FName& ChannelName);
    [[nodiscard]] TSharedRef<FGameRandomStream> GetOrCreateStreamShared(const FName& ChannelName);
    FGameRandomStream& InitializeStream(const FName& ChannelName, uint64 Seed);

    [[nodiscard]] bool HasStream(const FName& ChannelName) const;
    bool RemoveStream(const FName& ChannelName);
    void ClearAllStreams();

    [[nodiscard]] int32 GetStreamCount() const noexcept;
    int32 GetChannelNames(TArray<FName>& OutChannelNames) const;
    [[nodiscard]] FGameRandomManager& GetManager() noexcept;
    [[nodiscard]] const FGameRandomManager& GetManager() const noexcept;

    void ExportStreams(FArchive& Ar) const;
    bool ImportStreams(FArchive& Ar);

    static URandomSubsystem& Get(const UObject* WorldContextObject);
    static URandomSubsystem* GetPtr(const UObject* WorldContextObject);
};
```

- **线程**：`UWorldSubsystem` 生命周期回调在游戏线程执行；随机流本身仍非线程安全。
- **生命周期**：与当前 `UWorld` 强绑定，`Deinitialize()` 清空全部通道；首次创建通道时按规范化通道名派生确定性种子。
- **薄壳设计**：所有流管理与归档接口直接委托 `FGameRandomManager`，`GetManager()` 可取回纯 C++ 核心。
- **失败语义**：`Get` 在 `WorldContextObject` 无有效 `UWorld` 时触发 `check` 断言；不确定时用 `GetPtr`，无效上下文返回 `nullptr`。

```cpp
void AMyCharacter::BeginPlay() {
    Super::BeginPlay();

    if (URandomSubsystem* RngSys = URandomSubsystem::GetPtr(this)) {
        TSharedRef<FGameRandomStream> CombatRng = RngSys->GetOrCreateStreamShared(TEXT("Combat"));
        const int32 Roll = CombatRng->RandRange(1, 100);
    }
}
```

## 常见错误与边界

- **共享流**：同一 `FGameRandomStream` 被多线程并发抽取会破坏确定性，甚至产生数据竞争；按线程或系统分通道。
- **裸引用失效**：`RemoveStream` / `ClearAllStreams` 后，`GetOrCreateStream` 返回的引用失效，跨生命周期请用 `GetOrCreateStreamShared`。
- **空容器断言**：`TShuffleBag::Draw` 与 `TWeightedPicker::Pick` 在空容器上触发 `checkf`；不确定非空时用 `TryDraw` / `TryPick`。
- **指针跨次持有**：`TShuffleBag::TryDraw` 返回内部元素指针，下一次抽取交换即失效；需要稳定值请用 `Draw` 的拷贝。
- **权重非法**：`TWeightedPicker::AddEntry` 对非有限、非正权重返回 `false`，调用方需检查返回值。
- **导入回滚**：`ImportStreams` 失败不会部分写入；失败后既有通道状态保持导入前内容。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [确定性与回放 API](./Determinism.md)
- [随机与类型速查](./RandomAndTypes.md)
- [快速上手](../Guides/QuickStart.md)
