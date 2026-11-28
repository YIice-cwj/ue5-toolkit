# GameCore 基础类型与确定性随机 API 参考

> **一句话**：强类型结果、结构化错误、代际句柄与槽位表、唯一 ID、位掩码、可取消守卫，以及确定性随机设施的公共 API。
> **头文件**：`Types/` 与 `Random/` 下按需包含，见各节头文件行 · **线程**：值类型与容器均非线程安全；`FGameDistIdGenerator` 以 `FCriticalSection` 保护，`FGameLocalIdGenerator` 使用原子递增 · **失败语义**：`TGameResult` 在错误态访问成功值会触发断言；`FromString` 解析失败返回 `false`；`ImportStreams` 失败原子回滚

## 30 秒速览

- **结果与错误**：`TGameResult<TSuccess, TError = FGameError>` 提供 `Map` / `AndThen` / `MapError` / `OrElse` 管线；`GAME_TRY` / `GAME_TRY_CTX` 跨函数传播错误，`TGameStatusResult<T>` 是 `TGameResult<T, FGameError>` 的官方别名。
- **句柄与容器**：`TGameHandle<TTag>` 是 64 位代际句柄；`TSlotMap<TValue, TTag>` 以密集数组 + 稀疏槽位实现 O(1) 插入、删除与查找。
- **唯一 ID**：`FGameDistId` 为雪花布局，`FGameLocalId` 为原子自增；两者都支持雪崩哈希、字符串互转与归档序列化。
- **位集合与守卫**：`TBitMask<TEnum>` 以枚举值作为位索引；`TCancelableScopeGuard` 支持动态 `Dismiss()` 与移动转移。
- **随机设施**：`FGameRandomStream`、`URandomSubsystem`、`FPseudoRandomDistribution`、`TShuffleBag<T>`、`TWeightedPicker<T>`；完整参数见 [Random.md](./Random.md)。

> **迁移提示**：空间哈希索引 `TSpatialHashGrid2D` / `TSpatialHashGrid3D` 已迁至独立插件 `Plugins/GameSpatial`，不在本文范围。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `TGameResult::Success` / `Failure` | 构造成功或失败结果 | 值语义非线程安全；失败态访问成功值触发 `checkf` |
| `TGameResult::Map` / `AndThen` | 成功链式映射与扁平化绑定 | 失败短路透传错误，不执行闭包 |
| `TGameResult::OrElse` | 失败时调用恢复闭包 | 成功时原样透传 |
| `FGameError::Make` | 构造结构化错误 | `IsOk()` 要求 `Severity == Success` 且 `ErrorCode == 0` |
| `TSlotMap::Insert` / `Remove` | O(1) 插入/删除并下发代际句柄 | 非线程安全；`Find` 指针在下次结构变更后失效 |
| `TSlotMap::IsValidHandle` | 判断句柄对应槽位是否存活 | 唯一入口；`TGameHandle::IsValid()` 仅校验位域格式 |
| `FGameDistIdGenerator::NextId` | 生成雪花布局分布式 ID | `FCriticalSection` 保护；时间戳为负或超 41 位返回无效 ID |
| `FGameLocalIdGenerator::NextId` | 生成进程内自增 ID | 原子 `fetch_add` 无锁 |
| `TBitMask<TEnum>::Set` | 按枚举位索引置位 | 越界索引安全忽略；非线程安全 |
| `TCancelableScopeGuard::Dismiss` | 取消析构回调 | 非线程安全；移动赋值会先执行自身仍激活的回调 |

## 强类型结果 `TGameResult<TSuccess, TError>`

头文件：`#include "Types/TGameResult.h"`

```cpp
template <typename TSuccess, typename TError = FGameError>
class [[nodiscard]] TGameResult {
public:
    using SuccessType = TSuccess;
    using ErrorType = TError;

    [[nodiscard]] bool IsSuccess() const noexcept;
    [[nodiscard]] bool IsFailure() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;

    [[nodiscard]] const SuccessType& GetValue() const;
    [[nodiscard]] SuccessType& GetMutableValue();
    [[nodiscard]] SuccessType GetValueOr(const SuccessType& FallbackValue) const &;
    [[nodiscard]] SuccessType GetValueOr(SuccessType&& FallbackValue) const &;
    [[nodiscard]] SuccessType GetValueOr(const SuccessType& FallbackValue) &&;
    [[nodiscard]] SuccessType GetValueOr(SuccessType&& FallbackValue) &&;
    [[nodiscard]] SuccessType ExtractValue();
    [[nodiscard]] const ErrorType& GetError() const;
    [[nodiscard]] ErrorType ExtractError();

    template <typename F> auto Map(F&& Func) const &;
    template <typename F> auto Map(F&& Func) &&;
    template <typename F> auto AndThen(F&& Func) const &;
    template <typename F> auto AndThen(F&& Func) &&;
    template <typename F> auto MapError(F&& Func) const &;
    template <typename F> auto MapError(F&& Func) &&;
    template <typename F> TGameResult OrElse(F&& RecoveryFunc) const &;
    template <typename F> TGameResult OrElse(F&& RecoveryFunc) &&;

    [[nodiscard]] static TGameResult Success(SuccessType InValue);
    [[nodiscard]] static TGameResult Failure(ErrorType InError);

    template <typename E = TError, typename = std::enable_if_t<std::is_same_v<E, FGameError>>>
    [[nodiscard]] static TGameResult Failure(int32 InErrorCode, FString InMessage);
    template <typename E = TError, typename = std::enable_if_t<std::is_same_v<E, FGameError>>>
    [[nodiscard]] static TGameResult Failure(int32 InModuleId, int32 InErrorCode, FString InMessage);
    template <typename E = TError, typename = std::enable_if_t<std::is_same_v<E, FGameError>>>
    [[nodiscard]] static TGameResult Failure(
        int32 InModuleId, int32 InErrorCode, EGameErrorSeverity InSeverity, FString InMessage
    );
};

template <typename TSuccess>
using TGameStatusResult = TGameResult<TSuccess, FGameError>;

#define GAME_TRY(ResultExpr)
#define GAME_TRY_CTX(ResultExpr, ContextText)
```

<details>
<summary>展开接口语义</summary>

| 接口 | 说明 |
| :--- | :--- |
| `Success` / `Failure` | 构造成功或失败结果；`Failure` 的 `FGameError` 重载可直接给错误码与文案 |
| `IsSuccess` / `IsFailure` / `operator bool` | 状态判定，`operator bool` 为 `explicit` |
| `GetValue` / `GetMutableValue` / `operator*` / `operator->` | 访问成功载荷，调用前必须处于成功态 |
| `GetValueOr` | 成功返回载荷副本或移动值，失败返回回退值；四种重载覆盖左值与右值 |
| `ExtractValue` / `ExtractError` | 移动提取载荷，调用后原载荷处于被移走状态 |
| `Map` | 成功时映射为新值；闭包返回 `void` 时得到 `TGameResult<void, E>` |
| `AndThen` | 成功时调用返回新 `TGameResult` 的闭包做扁平化绑定；失败短路透传错误 |
| `MapError` | 失败时转换错误类型；成功原样透传 |
| `OrElse` | 失败时调用恢复闭包；成功原样透传 |
| `GAME_TRY` | 结果为失败时立即向上返回错误转发器 |
| `GAME_TRY_CTX` | 同上，并对 `FGameError` 追加 `WithContext` 上下文 |

</details>

- **线程**：值语义容器，非线程安全；跨线程传递需各自持有副本。
- **生命周期**：实参按值接收并移动存储；`ExtractValue` / `ExtractError` 会移走载荷。
- **失败语义**：失败态调用 `GetValue` / `GetMutableValue` / `ExtractValue`，或成功态调用 `GetError` / `ExtractError`，都会触发 `checkf`。
- **仅移动载荷**：对 `TUniquePtr` 等仅移动类型使用 `&&` 重载配合 `ExtractValue()` / `ExtractError()` 完成移动语义流水线。
- **同型实例化**：支持成功与错误类型相同的实例化，内部以标签区分存储。

```cpp
enum class EParseError : uint8 { EmptyInput, OutOfRange };

TGameResult<int32, EParseError> ParseLevel(const FString& Raw) {
    if (Raw.IsEmpty()) {
        return TGameResult<int32, EParseError>::Failure(EParseError::EmptyInput);
    }
    return TGameResult<int32, EParseError>::Success(FCString::Atoi(*Raw));
}

const FString Message = ParseLevel(TEXT("10"))
    .Map([](int32 Value) { return Value * 10; })
    .AndThen([](int32 Multiplied) -> TGameResult<FString, EParseError> {
        return Multiplied > 50
            ? TGameResult<FString, EParseError>::Success(FString::Printf(TEXT("High: %d"), Multiplied))
            : TGameResult<FString, EParseError>::Failure(EParseError::OutOfRange);
    })
    .OrElse([](EParseError) {
        return TGameResult<FString, EParseError>::Success(TEXT("Default: 0"));
    })
    .GetValue();

TGameResult<int32, EParseError> ProcessPlayerLevel(const FString& Raw) {
    auto Result = ParseLevel(Raw);
    GAME_TRY(Result);
    return TGameResult<int32, EParseError>::Success(Result.ExtractValue() * 2);
}
```

## 结构化错误 `FGameError`

头文件：`#include "Types/FGameError.h"`

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FGameError {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Error")
    int32 ModuleId = Game::ModuleId::Core;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Error")
    int32 ErrorCode = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Error")
    EGameErrorSeverity Severity = EGameErrorSeverity::Success;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Error")
    FString Message;

    FGameError() = default;
    explicit FGameError(FString InMessage);
    explicit FGameError(const TCHAR* InMessage);
    FGameError(int32 InModuleId, int32 InErrorCode, EGameErrorSeverity InSeverity, FString InMessage);

    [[nodiscard]] static FGameError Ok() noexcept;
    [[nodiscard]] static FGameError Make(
        int32 InModuleId,
        int32 InErrorCode,
        FString InMessage,
        EGameErrorSeverity InSeverity = EGameErrorSeverity::Error
    );

    [[nodiscard]] bool IsOk() const noexcept;
    [[nodiscard]] bool IsError() const noexcept;
    [[nodiscard]] bool IsCritical() const noexcept;
    [[nodiscard]] FGameError WithContext(const FString& InContext) const &;
    [[nodiscard]] FGameError WithContext(const FString& InContext) &&;
    [[nodiscard]] FString ToString() const;
};
```

- **线程**：纯值结构，非线程安全。
- **生命周期**：`USTRUCT(BlueprintType)`，可暴露到蓝图；默认构造为 `Success` / `ErrorCode == 0`。
- **成功判定**：`IsOk()` 仅在 `Severity == EGameErrorSeverity::Success` 且 `ErrorCode == 0` 时为 `true`。
- **上下文**：`WithContext` 在 `Message` 非空时追加为 `Message [Context]`，为空时直接使用上下文；右值重载复用字符串缓冲区。
- **模块语义**：`ModuleId` 取 `Game::ModuleId` 常量（如 `Core = 1`、`Asset = 3`），错误码取模块内自定义或 `Game::CommonErrorCodes`（如 `NotFound = 4`）。

```cpp
FGameError Err = FGameError::Make(
    Game::ModuleId::Asset,
    Game::CommonErrorCodes::NotFound,
    TEXT("Texture_01 missing"),
    EGameErrorSeverity::Error
);

FGameError Enriched = Err.WithContext(TEXT("LoadCharacterMesh"));
```

## 代际句柄 `TGameHandle` 与槽位表 `TSlotMap`

头文件：`#include "Types/TGameHandle.h"`、`#include "Containers/TSlotMap.h"`

```cpp
template <typename TTag>
struct TGameHandle {
    static constexpr uint32 INVALID_INDEX = MAX_uint32;

    constexpr TGameHandle() noexcept = default;
    constexpr TGameHandle(uint32 InIndex, uint32 InGeneration) noexcept;

    [[nodiscard]] constexpr bool IsValid() const noexcept;
    [[nodiscard]] constexpr explicit operator bool() const noexcept;
    [[nodiscard]] constexpr uint32 GetIndex() const noexcept;
    [[nodiscard]] constexpr uint32 GetGeneration() const noexcept;
    [[nodiscard]] constexpr uint64 PackUInt64() const noexcept;
    constexpr void Reset() noexcept;
    [[nodiscard]] constexpr bool operator==(const TGameHandle& Other) const noexcept = default;
    [[nodiscard]] constexpr bool operator<(const TGameHandle& Other) const noexcept;
    [[nodiscard]] static constexpr TGameHandle FromPackedUInt64(uint64 Packed) noexcept;
};

template <typename TTag>
[[nodiscard]] uint32 GetTypeHash(const TGameHandle<TTag>& Handle) noexcept;

template <typename TTag>
FArchive& operator<<(FArchive& Ar, TGameHandle<TTag>& Handle);

template <typename TValue, typename TTag = TValue>
class TSlotMap {
public:
    using ValueType = TValue;
    using HandleType = TGameHandle<TTag>;
    using SizeType = int32;

    void Reserve(SizeType Capacity);
    [[nodiscard]] HandleType Insert(ValueType InValue);

    template <typename... TArgs>
    [[nodiscard]] HandleType Emplace(TArgs&&... Args);

    bool Remove(HandleType Handle);
    [[nodiscard]] bool IsValidHandle(HandleType Handle) const noexcept;
    [[nodiscard]] ValueType* Find(HandleType Handle) noexcept;
    [[nodiscard]] const ValueType* Find(HandleType Handle) const noexcept;
    [[nodiscard]] ValueType& operator[](HandleType Handle);
    [[nodiscard]] const ValueType& operator[](HandleType Handle) const;

    void Reset();
    [[nodiscard]] SizeType Num() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] TArrayView<const ValueType> GetDenseValues() const noexcept;
    [[nodiscard]] TArrayView<ValueType> GetMutableDenseValues() noexcept;
    [[nodiscard]] HandleType GetHandleForDenseIndex(SizeType DenseIndex) const noexcept;

    template <typename TFunc>
    void ForEach(TFunc&& Func);

    template <typename TFunc>
    void ForEach(TFunc&& Func) const;
};
```

| 接口 | 说明 |
| :--- | :--- |
| `TGameHandle(Index, Generation)` | 高 32 位代际号，低 32 位槽位索引；`IsValid()` 仅校验位域格式 |
| `PackUInt64` / `FromPackedUInt64` | 64 位打包与还原，可用于序列化或外部索引 |
| `Insert` / `Emplace` | 返回新下发的代际句柄，O(1) |
| `Remove` | 尾部交换弹出保持密集数组连续，槽位代际自增；过期句柄返回 `false` |
| `IsValidHandle` | 校验槽位占用与代际号，是判断句柄是否存活的唯一入口 |
| `Find` / `operator[]` | 返回元素指针或引用；无效句柄 `Find` 返回 `nullptr`，`operator[]` 触发 `checkf` |
| `GetDenseValues` / `GetMutableDenseValues` | 直接暴露连续密集数组视图，供缓存友好遍历 |
| `GetHandleForDenseIndex` | 由密集索引反查句柄，越界返回无效句柄 |
| `ForEach` | 同时传出句柄与元素引用；回调返回 `false` 可提前终止 |
| `Reset` | 清空全部元素并使此前签发的句柄全部失效 |

- **线程**：非线程安全；`ForEach` 用原子迭代深度检测遍历期间的非法结构修改，但不提供并发写保护。
- **生命周期**：`Find` 返回的指针在下一次 `Insert` / `Remove` / `Reset` 后失效，禁止跨结构变更缓存。
- **遍历约束**：`ForEach` 回调内禁止对本容器执行 `Insert` / `Remove` / `Reset`，否则触发 `checkf`。
- **代际上限**：槽位代际达到 `MAX_uint32` 后退役（代际置 0），不再复用，杜绝 ABA。
- **失败语义**：无效或过期句柄的 `Remove` 返回 `false`，`Find` 返回 `nullptr`。

```cpp
struct FBuffSlotTag {};
using FBuffHandle = TGameHandle<FBuffSlotTag>;

TSlotMap<float, FBuffSlotTag> ActiveBuffTimers;
const FBuffHandle Handle = ActiveBuffTimers.Insert(5.0f);

if (float* Remaining = ActiveBuffTimers.Find(Handle)) {
    *Remaining -= 0.1f;
}

ActiveBuffTimers.ForEach([](FBuffHandle CurHandle, float& RemainingTime) {
    RemainingTime -= 0.016f;
});

ActiveBuffTimers.Remove(Handle);
```

## 唯一 ID `FGameDistId` / `FGameLocalId`

头文件：`#include "Types/FGameDistId.h"`、`#include "Types/FGameDistIdGenerator.h"`、`#include "Types/FGameLocalId.h"`、`#include "Types/FGameLocalIdGenerator.h"`

```cpp
struct GAMECORE_API FGameDistId {
    static constexpr uint8 SEQUENCE_BITS = 12;
    static constexpr uint8 NODE_ID_BITS = 10;
    static constexpr uint8 TIMESTAMP_BITS = 41;

    uint64 RawValue = 0;

    constexpr FGameDistId() noexcept = default;
    constexpr explicit FGameDistId(uint64 InRawValue) noexcept;

    [[nodiscard]] constexpr bool IsValid() const noexcept;
    [[nodiscard]] constexpr uint64 GetValue() const noexcept;
    [[nodiscard]] constexpr uint16 GetNodeId() const noexcept;
    [[nodiscard]] constexpr uint16 GetSequence() const noexcept;
    [[nodiscard]] constexpr int64 GetTimestampDelta() const noexcept;
    [[nodiscard]] constexpr int64 GetTimestampMilliseconds(int64 CustomEpochMs) const noexcept;
    [[nodiscard]] FString ToString() const;
    [[nodiscard]] FString ToHex() const;
    static bool FromString(FStringView InStr, FGameDistId& OutId) noexcept;
    [[nodiscard]] static constexpr FGameDistId FromParts(
        int64 TimestampDelta, uint16 NodeId, uint16 Sequence
    ) noexcept;
};

class GAMECORE_API FGameDistIdGenerator {
public:
    using FClockSource = TFunction<int64()>;

    static constexpr int64 DEFAULT_CUSTOM_EPOCH = 1735689600000LL;
    static constexpr int32 MAX_WAIT_SPINS = 1000;

    explicit FGameDistIdGenerator(uint16 InNodeId, int64 InCustomEpoch = DEFAULT_CUSTOM_EPOCH) noexcept;
    void Initialize(uint16 InNodeId, int64 InCustomEpoch = DEFAULT_CUSTOM_EPOCH) noexcept;
    void SetClockSource(FClockSource InClockSource);
    [[nodiscard]] FGameDistId NextId();
    [[nodiscard]] uint16 GetNodeId() const noexcept;
    [[nodiscard]] int64 GetCustomEpoch() const noexcept;
    [[nodiscard]] static FGameDistIdGenerator& Get();
};

struct GAMECORE_API FGameLocalId {
    uint64 Value = 0ULL;

    constexpr FGameLocalId() noexcept = default;
    constexpr explicit FGameLocalId(uint64 InValue) noexcept;

    [[nodiscard]] static constexpr FGameLocalId FromDistId(const FGameDistId& InDistId) noexcept;
    [[nodiscard]] constexpr bool IsValid() const noexcept;
    [[nodiscard]] constexpr uint64 GetValue() const noexcept;
    [[nodiscard]] constexpr FGameDistId ToDistId() const noexcept;
    [[nodiscard]] FString ToString() const;
    [[nodiscard]] FString ToHex() const;
    static bool FromString(FStringView InStr, FGameLocalId& OutId) noexcept;
};

class GAMECORE_API FGameLocalIdGenerator {
public:
    [[nodiscard]] static FGameLocalId NextId() noexcept;
    [[nodiscard]] static uint64 PeekCurrent() noexcept;
    static void ResetForTesting(uint64 InitialValue = 1ULL) noexcept;
};
```

| 类型 | 布局 / 机制 | 适用场景 |
| :--- | :--- | :--- |
| `FGameDistId` | `[1-bit 保留] + [41-bit 相对毫秒] + [10-bit 节点] + [12-bit 序号]`，单毫秒最多 4096 个 | 多节点、跨服协同与持久化记录 |
| `FGameLocalId` | 纯 64 位原子自增，无时间戳与节点开销 | 进程内、单机与客户端运行时会话 |

- **线程**：`FGameDistIdGenerator::NextId` 由 `FCriticalSection` 保护；`FGameLocalIdGenerator::NextId` 使用松弛原子 `fetch_add`，无锁。
- **时钟处理**：分布式生成器在时钟停滞或回拨且单毫秒序号耗尽时自旋等待，超过 `MAX_WAIT_SPINS` 后推进逻辑毫秒防死锁。
- **失败语义**：`NextId` 在时间戳为负或超出 41 位上限时返回无效 ID；`FromString` 解析失败返回 `false` 且不修改输出。
- **哈希与序列化**：两者重载 `GetTypeHash`（`Game::Hash::Hash64To32`，MurmurHash3 64-bit Finalizer）与 `operator<<`，可直接作为 `TMap` / `TSet` 键。
- **互转**：`LocalId.ToDistId()` 与 `FGameLocalId::FromDistId(DistId)` 为原始 64 位值的零成本映射。
- **测试接口**：`ResetForTesting` 仅用于自动化测试与世界会话初始化；传入 0 会被钳制为 1。

```cpp
FGameDistId DistId = FGameDistIdGenerator::Get().NextId();
const int64 CreatedTimeMs = DistId.GetTimestampMilliseconds(
    FGameDistIdGenerator::DEFAULT_CUSTOM_EPOCH
);
const uint16 NodeId = DistId.GetNodeId();

FGameLocalId LocalId = FGameLocalIdGenerator::NextId();
const FString LocalHex = LocalId.ToHex();

TMap<FGameLocalId, FString> EntityMap;
EntityMap.Add(LocalId, TEXT("PlayerActor"));
```

## 位掩码 `TBitMask<TEnum>`

头文件：`#include "Types/TBitMask.h"`

```cpp
template <typename TEnum>
class TBitMask {
    static_assert(std::is_enum_v<TEnum>, "TBitMask 仅支持枚举类型");

public:
    using FUnderlying = std::underlying_type_t<TEnum>;
    using FUnsignedUnderlying = std::make_unsigned_t<FUnderlying>;

    static constexpr size_t BIT_WIDTH = sizeof(FUnderlying) * 8;

    constexpr TBitMask() noexcept;
    constexpr explicit TBitMask(TEnum InBit) noexcept;
    constexpr explicit TBitMask(FUnderlying InRawMask) noexcept;

    [[nodiscard]] static constexpr TBitMask FromRawMask(FUnderlying InRawMask) noexcept;

    [[nodiscard]] constexpr bool Has(TEnum InBit) const noexcept;
    constexpr TBitMask& Set(TEnum InBit) noexcept;
    constexpr TBitMask& Clear(TEnum InBit) noexcept;
    constexpr TBitMask& Toggle(TEnum InBit) noexcept;

    [[nodiscard]] constexpr bool HasAny(const TBitMask& Other) const noexcept;
    [[nodiscard]] constexpr bool HasAll(const TBitMask& Other) const noexcept;
    [[nodiscard]] constexpr bool HasNone(const TBitMask& Other) const noexcept;
    [[nodiscard]] constexpr bool IsEmpty() const noexcept;
    constexpr void Clear() noexcept;
    [[nodiscard]] constexpr FUnderlying GetRawMask() const noexcept;

    constexpr TBitMask operator|(const TBitMask& Other) const noexcept;
    constexpr TBitMask operator&(const TBitMask& Other) const noexcept;
    constexpr TBitMask operator^(const TBitMask& Other) const noexcept;
    constexpr TBitMask& operator|=(const TBitMask& Other) noexcept;
    constexpr TBitMask& operator&=(const TBitMask& Other) noexcept;
    constexpr TBitMask& operator^=(const TBitMask& Other) noexcept;
    constexpr TBitMask operator~() const noexcept;
};

template <typename TEnum>
[[nodiscard]] uint32 GetTypeHash(const TBitMask<TEnum>& Mask) noexcept;
```

- **位索引契约**：构造、`Set`、`Has`、`Toggle` 的 `TEnum` 实参表示位索引（0、1、2…），内部映射为 `1 << BitIndex`；原生掩码请用 `FromRawMask` 或 `TBitMask(FUnderlying)`。
- **越界防护**：负数或 `>= BIT_WIDTH` 的位索引会被安全忽略并返回 0 掩码，不产生未定义移位。
- **线程**：底层类型为枚举底层整数（`uint8` ~ `uint64`）的值类型，非线程安全；跨线程共享需外部同步。
- **容器支持**：重载 `GetTypeHash`，可直接作为 `TMap` / `TSet` 键。

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

## 可取消守卫 `TCancelableScopeGuard`

头文件：`#include "Types/TCancelableScopeGuard.h"`

```cpp
template <typename TFunc>
class [[nodiscard]] TCancelableScopeGuard {
public:
    explicit TCancelableScopeGuard(TFunc InFunc) noexcept;
    ~TCancelableScopeGuard();

    TCancelableScopeGuard(const TCancelableScopeGuard&) = delete;
    TCancelableScopeGuard& operator=(const TCancelableScopeGuard&) = delete;

    TCancelableScopeGuard(TCancelableScopeGuard&& Other) noexcept;
    TCancelableScopeGuard& operator=(TCancelableScopeGuard&& Other) noexcept;

    void Dismiss() noexcept;
    [[nodiscard]] bool IsActive() const noexcept;
};

template <typename TFunc>
[[nodiscard]] auto MakeCancelableScopeGuard(TFunc&& Func);
```

- **线程**：非线程安全，守卫应与其保护的业务在同一线程栈上。
- **生命周期**：析构时若仍激活则执行回调；移动构造转移回调并解除源对象激活状态；移动赋值会先执行自身未取消的回调，再接管源状态。
- **取消语义**：`Dismiss()` 后析构不再触发回调；`IsActive()` 可用于查询。
- **零堆分配**：守卫自身不分配堆内存，闭包按值内联存储在守卫对象中。
- **对比 `ON_SCOPE_EXIT`**：额外支持动态取消与移动语义转移。

```cpp
bool PerformComplexTransaction() {
    AcquireResourceA();
    auto Rollback = MakeCancelableScopeGuard([]() {
        ReleaseResourceA();
    });

    if (!StepTwo()) {
        return false;
    }

    Rollback.Dismiss();
    return true;
}
```

## 确定性随机设施

头文件：`#include "Random/FGameRandomStream.h"`、`#include "Random/URandomSubsystem.h"`、`#include "Random/FPseudoRandomDistribution.h"`、`#include "Random/TShuffleBag.h"`、`#include "Random/TWeightedPicker.h"`

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FGameRandomStream {
    GENERATED_BODY()

    static constexpr uint64 DEFAULT_SEED = 0x853C49E6748FEA9BULL;

    FGameRandomStream() noexcept;
    explicit FGameRandomStream(uint64 InSeed) noexcept;

    [[nodiscard]] int32 RandRange(int32 Min, int32 Max) noexcept;
    [[nodiscard]] float FRand() noexcept;
    [[nodiscard]] bool RollChance(float Probability) noexcept;
    [[nodiscard]] uint64 GetState() const noexcept;
    void SetState(uint64 InState) noexcept;
    void SerializeState(FArchive& Ar);
};

UCLASS()
class GAMECORE_API URandomSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

public:
    [[nodiscard]] FGameRandomStream& GetOrCreateStream(const FName& ChannelName);
    [[nodiscard]] TSharedRef<FGameRandomStream> GetOrCreateStreamShared(const FName& ChannelName);
    FGameRandomStream& InitializeStream(const FName& ChannelName, uint64 Seed);
    void ExportStreams(FArchive& Ar) const;
    bool ImportStreams(FArchive& Ar);

    static URandomSubsystem& Get(const UObject* WorldContextObject);
    static URandomSubsystem* GetPtr(const UObject* WorldContextObject);
};

USTRUCT(BlueprintType)
struct GAMECORE_API FPseudoRandomDistribution {
    GENERATED_BODY()

    explicit FPseudoRandomDistribution(float InNominalProbability) noexcept;
    void SetNominalProbability(float InNominalProbability) noexcept;
    [[nodiscard]] float GetNextRollProbability() const noexcept;
    [[nodiscard]] bool Roll(FGameRandomStream& Stream) noexcept;
    void Reset() noexcept;
};

template <typename TValue>
class TShuffleBag {
public:
    using ValueType = TValue;
    using SizeType = int32;

    void Add(const ValueType& Item, SizeType Count = 1);
    [[nodiscard]] ValueType Draw(FGameRandomStream& Stream);
    [[nodiscard]] const ValueType* TryDraw(FGameRandomStream& Stream);
    [[nodiscard]] SizeType Remaining() const noexcept;
};

template <typename TValue>
class TWeightedPicker {
public:
    using ValueType = TValue;
    using SizeType = int32;

    bool AddEntry(ValueType Item, double Weight);
    void Build() const;
    [[nodiscard]] const ValueType* TryPick(FGameRandomStream& Stream) const;
    [[nodiscard]] const ValueType& Pick(FGameRandomStream& Stream) const;
};
```

| 组件 | 适用场景 | 算法与复杂度 |
| :--- | :--- | :--- |
| `FPseudoRandomDistribution` | 暴击、闪避、被动触发防连黑/连爆 | 二分反解马尔可夫链稳态期望概率常数 C，未命中线性累加，命中归零 |
| `TShuffleBag<T>` | 音效轮播、保底卡池、固定配比掉落 | 不放回抽样袋，抽空自动重置游标；就地随机交换（等价部分 Fisher-Yates）O(1) |
| `TWeightedPicker<T>` | 大型掉落表、高频 AI 行为权重选择 | Vose's Alias Method，O(N) 预构建/惰性构建，单次采样 O(1) |

- **线程**：随机流、分布与容器均非线程安全；`URandomSubsystem` 本身不加锁。
- **确定性**：`FGameRandomStream` 使用 PCG32 整数推进与 SplitMix64 种子混合，跨平台位级一致。
- **生命周期**：`URandomSubsystem` 与 `UWorld` 绑定；`GetOrCreateStream` 的裸引用在通道移除后失效，跨生命周期请用 `GetOrCreateStreamShared`。
- **失败语义**：`URandomSubsystem::Get` 在无有效 `UWorld` 时触发 `check` 断言，不确定时用 `GetPtr`；`TShuffleBag::Draw` 与 `TWeightedPicker::Pick` 在空容器上触发 `checkf`，安全路径用 `TryDraw` / `TryPick`。
- **完整参数与示例**：见 [Random.md](./Random.md)。

```cpp
URandomSubsystem& RngSys = URandomSubsystem::Get(GetWorld());
RngSys.InitializeStream(TEXT("Combat.Crit"), 20261106ULL);

FGameRandomStream& CombatStream = RngSys.GetOrCreateStream(TEXT("Combat.Crit"));
const bool bCrit = CombatStream.RollChance(0.25f);
const int32 DamageRoll = CombatStream.RandRange(95, 105);
```

## 常见错误与边界

- **未检查结果**：`TGameResult` 标记 `[[nodiscard]]`；失败态调用 `GetValue` / `ExtractValue` 会触发 `checkf`。
- **句柄存活判定**：`TGameHandle::IsValid()` 只校验位域格式，判断槽位是否存活必须用 `TSlotMap::IsValidHandle`。
- **指针失效**：`TSlotMap::Find` 返回的指针在下一次 `Insert` / `Remove` / `Reset` 后失效；`ForEach` 内禁止结构修改。
- **位掩码语义**：`TBitMask(EBuffChannel::Combat)` 设置的是位索引 0，不是原始掩码 `0x01`；直接使用位模式请走 `FromRawMask`。
- **守卫移动**：`TCancelableScopeGuard` 移动赋值会先执行自身仍激活的回调，容易造成重复清理；转移所有权前先 `Dismiss()`。
- **ID 测试重置**：`FGameLocalIdGenerator::ResetForTesting` 会重置全局序列，仅限测试或会话初始化，禁止在运行期业务路径调用。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [确定性随机 API](./Random.md)
- [并发容器 API](./Containers.md)
- [快速上手](../Guides/QuickStart.md)
