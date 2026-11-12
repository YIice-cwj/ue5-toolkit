# GameCore 确定性随机数与洗牌算法 API 参考

本文档汇总 `GameCore` 提供的工业级确定性伪随机数生成、多通道流隔离、防连黑平滑分布与抽卡算法容器（全部位于 `Random/` 目录）：
- `FGameRandomStream`：PCG32 确定性隔离随机流、SplitMix64 种子雪崩、Lemire 无偏区间映射与三维立体角采样
- `FGameRandomManager`：纯 C++ 多通道隔离随机数流管理器（零 UWorld 依赖、地址稳定、确定性字典序二进制归档）
- `FPseudoRandomDistribution`：Dota 2 标准 PRD 防脸黑平滑分布状态机
- `TShuffleBag<T>`：泛型不放回抽取洗牌袋（就地 Swap $O(1)$ 抽取与耗尽自动重洗）
- `TWeightedPicker<T>`：基于 Vose's Alias Method 的 $O(1)$ 权重随机抽取器
- `URandomSubsystem`：WorldSubsystem 引擎外壳薄壳桥接

---

## 1. 确定性隔离随机流 `FGameRandomStream`

头文件：`#include "Random/FGameRandomStream.h"`

### 核心特性与算法原理
- **PCG32 整数同余生成器**：采用 PCG-XSH-RR（XOR-Shift 结合右旋位运算）状态推进，64 位内部状态、32 位高质量均匀输出，跨 Windows / Mac / Linux / 移动平台实现 100% 位级确定性一致，彻底消除标准库 `std::mt19937` 或平台浮点实现的随机序列分叉；
- **SplitMix64 种子雪崩混合**：使用黄金分割比常数 `0x9E3779B97F4A7C15ULL` 结合双重雪崩散列，即便传入连续递增的种子（如 1, 2, 3），也能瞬间散列为高熵非零初始状态；
- **Daniel Lemire 无偏区间映射（Debiased Integer Multiplication）**：
  - 传统 `rand() % Bound` 存在严重的低位取模偏差（Modulo Bias）；
  - `NextBoundedUInt32(Bound)` 通过计算拒绝采样阈值 `(0u - Bound) % Bound` 消除偏差，数学上严格保证 $[0, Bound)$ 内每个离散整数出现的概率绝对均等；
- **三维立体角均匀分布采样**：
  - `RandomUnitVector()`：在三维单位球面上以立体角面积微元均匀采样（非立方体截断归一化，杜绝立方体对角线方向的过采样偏差）；
  - `RandomVectorInConeRad(ConeDir, ConeHalfAngleRad)`：在指定半张角圆锥体内均匀采样发散方向向量，完美适用于弹道散布、霰弹枪散射与法线半球采样；
- **状态快照与二进制归档**：
  - `GetState()` / `SetState(State)`：支持保存/恢复 64 位内部状态快照，用于联机回放快照对齐；
  - 提供 `operator<<` 与 `SerializeState(FArchive& Ar)`，支持直接写入虚幻标准存档与网络数据包。

```cpp
// 初始化确定性种子
FGameRandomStream Rng(12345678ULL);

// 1. 无偏整数与浮点抽样
const int32 DiceRoll = Rng.RandRange(1, 6);       // [1, 6] 包含两端
const float NormalizedRate = Rng.FRand();        // [0.0f, 1.0f)
const bool bTriggered = Rng.RollChance(0.35f);    // 35% 成功率

// 2. 枪械弹道散布（在 5 度半张角锥形内均匀采样射击方向）
const FVector MuzzleDir = FVector::ForwardVector;
const float SpreadAngleRad = FMath::DegreesToRadians(5.0f);
const FVector ShotDir = Rng.RandomVectorInConeRad(MuzzleDir, SpreadAngleRad);

// 3. 状态快照与回滚
const uint64 SavedState = Rng.GetState();
Rng.RandRange(1, 100); // 消耗流
Rng.SetState(SavedState); // 回滚到之前状态，下一次抽样 100% 复现
```

---

## 2. 纯 C++ 多通道隔离随机数管理器 `FGameRandomManager`

头文件：`#include "Random/FGameRandomManager.h"`

### 职责与架构设计
- **零引擎 UWorld 依赖**：纯 C++ 算法核心，可在任意独立命令行工具、子线程、微服务或纯单元测试中独立实例化运行；
- **业务通道流隔离**：按业务命名通道（如 `Combat` / `Loot` / `AI` / `Cosmetic`）划分独立的 `FGameRandomStream`，各通道推进互不干扰，彻底避免 UI 飘字或环境粒子抽样污染战斗逻辑序列；
- **指针与引用物理地址绝对稳定**：内部存储采用 `TSharedRef<FGameRandomStream>`，`GetOrCreateStream` 获取的引用在后续增减其他通道导致 `TMap` 扩容时，底层内存地址 100% 保持稳定不变；
- **字典序确定性二进制归档**：
  - `ExportStreams` 在导出前强制对通道名执行字典序排序（`FNameLexicalLess`），保证多平台多节点导出的二进制数据字节完全幂等；
  - `ImportStreams` 校验魔数 `0x47524E47`（`GRNG`）、版本号与通道上限（4096），并在解析出错时进行原子回滚，杜绝半写入污染。

```cpp
FGameRandomManager Manager;

// 1. 按通道名获取独立流（未注册时自动按通道名 CRC 派生初始种子）
FGameRandomStream& CombatRng = Manager.GetOrCreateStream(TEXT("Combat"));
FGameRandomStream& LootRng = Manager.GetOrCreateStream(TEXT("Loot"));

// 2. 显式重新初始化特定通道种子
Manager.InitializeStream(TEXT("Loot"), 9999ULL);

// 3. 序列化至二进制缓冲区
FBufferArchive Ar;
Manager.ExportStreams(Ar);

// 4. 从二进制缓冲区恢复
FMemoryReader Reader(Ar);
FGameRandomManager RestoredManager;
bool bImportSuccess = RestoredManager.ImportStreams(Reader);
```

---

## 3. 伪随机平滑分布器 `FPseudoRandomDistribution`（PRD）

头文件：`#include "Random/FPseudoRandomDistribution.h"`

### 算法背景与数学模型
在游戏暴击、被动技能、异常状态附加等概率事件中，纯独立伯努利试验容易产生“连续 20 次不出暴击”或“连续 5 次暴击”的极端体验。
`GameCore` 实现工业级 PRD 模型：
- 设标称暴击概率为 $P$（如 20% = 0.2）；
- 每次判定成功率设定为 $(N + 1) \times C$，其中 $N$ 为当前连续未命中次数，$C$ 为步长常数；
- 若未命中，$N$ 自增 1，下一次判定概率线性提升；
- 一旦命中，$N$ 立即归零回滚；
- 内部通过 40 轮双精度二分逼近，求解马尔可夫链稳态长期宏观期望概率恰好等于 $P$ 的常数 $C$；
- 既从数学上保证宏观总体触发频率完全严格等于策划设定的 $P$，又从微观体验上彻底消除了极端连黑与连爆。

```cpp
// 初始化 20% 暴击率的 PRD 状态机
FPseudoRandomDistribution CritPRD(0.20f);

// 关联指定随机流执行判定
FGameRandomStream& Rng = Manager.GetOrCreateStream(TEXT("Combat"));
for (int32 Hit = 0; Hit < 10; ++Hit) {
    if (CritPRD.Roll(Rng)) {
        // 触发暴击！内部自动重置连续未命中计数
    } else {
        // 未命中，内部递增计数，下一次命中概率自动提升
    }
}
```

---

## 4. 泛型抽取不放回洗牌袋 `TShuffleBag<T>`

头文件：`#include "Random/TShuffleBag.h"`

### 核心特性
- **就地 Swap $O(1)$ 抽取**：每次抽取随机选择未抽出区间的一个元素，与区间末尾元素交换并将游标前移，时间复杂度恒定为 $O(1)$；
- **抽空自动重洗**：当当前轮次元素全部抽完时，自动重置有效游标开启新一轮洗牌周期，无需重新分配内存；
- **安全动态并入**：在抽取过程中调用 `Add` / `Emplace` 追加新元素时，新元素立即加入当前可抽取池，绝不打乱或回退本轮已抽出的历史。

```cpp
// 掉落保底卡池
TShuffleBag<FString> CardBag;
CardBag.Add(TEXT("Common"), 70);
CardBag.Add(TEXT("Rare"), 25);
CardBag.Add(TEXT("Epic"), 5);

// 抽取卡牌（抽空一轮自动循环洗牌）
FGameRandomStream& Rng = Manager.GetOrCreateStream(TEXT("Gacha"));
const FString& PulledCard = CardBag.Draw(Rng);
```

---

## 5. 恒定时间权重抽取器 `TWeightedPicker<T>`

头文件：`#include "Random/TWeightedPicker.h"`

### 核心特性
- **Vose's Alias Method（沃斯别名法）**：
  - 传统权重轮盘赌查找复杂度为 $O(N)$ 或 $O(\log N)$；
  - 本容器在 $O(N)$ 预构建/惰性构建别名表后，**单次权重抽取时间严格为 $O(1)$**，与条目数量无关；
- **防数值截断与权重保护**：严格校验输入权重（必须大于 0 且有限），支持 `TryPick` 安全指针接口、`Pick` 强契约引用接口与 `PickValue` 安全值拷贝接口。

```cpp
TWeightedPicker<FName> EnemySpawnTable;
EnemySpawnTable.AddEntry(TEXT("Goblin"), 60.0);
EnemySpawnTable.AddEntry(TEXT("Orc"), 30.0);
EnemySpawnTable.AddEntry(TEXT("Dragon"), 10.0);

// 显式预构建（也可在首次 Pick 时惰性构建）
EnemySpawnTable.Build();

// 极速 O(1) 权重轮盘抽取（值语义安全拷贝）
const FName ChosenEnemy = EnemySpawnTable.PickValue(Rng);
```

---

## 6. 世界级随机子系统 `URandomSubsystem`

头文件：`#include "Random/URandomSubsystem.h"`

### 引擎集成方式
- 继承自 `UWorldSubsystem`，生命周期与当前关卡世界强绑定；
- 采用 Thin-Shell 设计，将所有流管理直接委托给底层 `FGameRandomManager`；
- 提供 `GetOrCreateStreamShared` 返回 `TSharedRef<FGameRandomStream>`，确保跨通道移除与重载生命周期绝对安全；
- 提供静态便捷获取方法 `URandomSubsystem::Get(WorldContextObject)` 与安全空指针检查 `URandomSubsystem::GetPtr(...)`。

```cpp
void AMyCharacter::BeginPlay() {
    Super::BeginPlay();

    // 安全获取子系统并持有共享随机流句柄（跨通道卸载内存安全）
    if (URandomSubsystem* RngSys = URandomSubsystem::GetPtr(this)) {
        TSharedRef<FGameRandomStream> CombatRng = RngSys->GetOrCreateStreamShared(TEXT("Combat"));
        const int32 Roll = CombatRng->RandRange(1, 100);
    }
}
```
