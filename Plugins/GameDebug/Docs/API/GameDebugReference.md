# GameDebug API 参考

> **一句话**：GM 指令、域隔离屏幕打印、3D 绘制、轨迹记录、时间控制、观察中心与作用域秒表的公共 API。
> **头文件**：`GameDebug.h`（聚合）或按组件包含
> **线程**：控制台绑定、屏幕打印、3D 绘制、时间控制与观察面板刷新依赖 GameThread；注册表与观察中心内部加锁
> **失败语义**：多数入口返回 `bool`；编译开关关闭时宏展开为 `((void)0)` 且实参不求值

## 30 秒速览

- **GM 指令**：`FGameCheatRegistry` 绑定 `IConsoleManager`（`ECVF_Cheat`），受 `Game::Config::bEnableGM` 控制，Shipping 物理剥离。
- **屏幕诊断**：`FGameDebugPrint` 提供追加 / 通道原地刷新 / 警告 / 错误；`FGameDebugWatchHub` 按分类观察实时变量。
- **3D 可视化**：`FGameDebugDraw` 提供 9 类图元；`FGameDebugTrailRecorder` 以定长环形缓冲记录运动轨迹。
- **时间控制**：`FGameDebugTimeController` 按 `UWorld` 隔离时间膨胀、暂停与逐帧步进。
- **耗时诊断**：`FScopedDebugStopwatch` 为 RAII 作用域秒表，`GAME_ENABLE_CPU_PROFILER == 0` 时成员与时钟全部移除。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FGameCheatRegistry::RegisterCheat` | 注册 GM 控制台指令 | 仅 GameThread；重名或空回调返回 `false`，Shipping 剥离 |
| `FGameCheatRegistry::ExecuteCheat` | 执行 GM 命令行 | 支持引号含空格参数与转义；锁外派发 |
| `FGameDebugPrint::Print` | 追加或按 Key 原地刷新屏幕消息 | 仅 GameThread；负 / 非有限 `Duration` 钳制为 `0` |
| `FGameDebugPrint::PrintChannel` | 按通道域原地刷新 | 64 位域盐键；与 `WatchHub` 槽位正交 |
| `FGameDebugDraw::DrawLine` | 绘制调试线段（9 类图元之一） | 仅 GameThread；非法输入静默跳过 |
| `FGameDebugTrailRecorder::AddSample` | 记录运动轨迹采样点 | 单实例非线程安全；拦截非有限时间戳 |
| `FGameDebugTimeController::SetTimeDilation` | 设置指定世界时间膨胀 | 仅 GameThread；钳制至 `[0.001f, 20.0f]` |
| `FGameDebugTimeController::StepFrame` | 单帧步进推进 | 仅 GameThread；帧数下限钳制为 1 |
| `FGameDebugWatchHub::Watch` | 注册实时变量观察项 | 同分类同名原地替换；闭包为空返回 `false` |
| `FScopedDebugStopwatch` | RAII 作用域耗时秒表 | 析构超阈告警；Profiler 关闭时零成员零开销 |

## 编译期开关

| 组件 | 开关 | 关闭 / Shipping 行为 |
| :--- | :--- | :--- |
| `FGameCheatRegistry` | `Game::Config::bEnableGM` | 物理剥离，注册返回 `false`，其余入口无操作 |
| `FGameDebugPrint` | `GAME_ENABLE_DEBUG_DRAW` / `bEnableDebugDraw` | `GAME_DEBUG_PRINT*` 展开为 `((void)0)`，实参不求值 |
| `FGameDebugDraw` | `GAME_ENABLE_DEBUG_DRAW` / `bEnableDebugDraw` | `GAME_DEBUG_DRAW_*` 展开为 `((void)0)`，绘制不执行 |
| `FGameDebugTrailRecorder` | `Game::Config::bEnableDebugDraw` | Shipping 跳过环形缓冲堆分配 |
| `FGameDebugTimeController` | `bEnableGM || bEnableDebugDraw` | 入口短路；`GM.Time*` 注册仅受 `bEnableGM` 控制 |
| `FGameDebugWatchHub` | `GAME_ENABLE_DEBUG_DRAW` / `bEnableDebugDraw` | `GAME_DEBUG_WATCH*` 展开为 `((void)0)` |
| `FScopedDebugStopwatch` | `GAME_ENABLE_CPU_PROFILER` | 全部成员变量被裁剪，`sizeof == 1`，零构造 / 析构时钟开销 |

## GM 指令注册 `FGameCheatRegistry`

```cpp
class GAMEDEBUG_API FGameCheatRegistry final {
public:
    using FCheatHandler = TFunction<void(const TArray<FString>&)>;

    bool RegisterCheat(FName CommandName, const FString& HelpText, FCheatHandler Handler);
    bool UnregisterCheat(FName CommandName);
    bool ExecuteCheat(FStringView CommandLine);

    [[nodiscard]] bool HasCheat(FName CommandName) const;
    [[nodiscard]] int32 GetCheatCount() const;
    void ClearAll();

    [[nodiscard]] static FGameCheatRegistry& Get();
};
```

| 接口 | 参数 | 返回 / 契约 |
| :--- | :--- | :--- |
| `RegisterCheat` | `CommandName`：唯一名称；`HelpText`：帮助文案；`Handler`：参数回调 | 拒绝 `NAME_None`、含任意空白字符的名称、空回调或重名；锁外绑定 `IConsoleManager`；引擎已存在同名对象时记 `Warning` 并仍支持 `ExecuteCheat` |
| `UnregisterCheat` | `CommandName` | 存在并成功注销返回 `true`；锁外同步卸载绑定的 `IConsoleObject` |
| `ExecuteCheat` | `CommandLine`：完整命令行视图 | 零分配前置命令查找；支持双引号含空格参数、空引号 `""` 与 `\"` / `\\` 转义；锁外派发 |
| `HasCheat` / `GetCheatCount` | 名称 / 无 | 读锁线程安全查询 |
| `ClearAll` | 无 | 卸载全部由注册表创建的控制台命令并清空映射 |
| `Get` | 无 | 获取全局单例引用 |

- **线程**：`RegistryLock` 保护映射，`ConsoleSyncLock` 串行化控制台写路径；与控制台交互在锁外执行。
- **生命周期**：模块卸载或测试清理时调用 `ClearAll()`。
- **失败语义**：GM 开关关闭、名称非法或重名时 `RegisterCheat` 返回 `false`，不抛异常。

```cpp
#include "Cheat/FGameCheatRegistry.h"

FGameCheatRegistry::Get().RegisterCheat(
    FName(TEXT("GM.GiveItem")),
    TEXT("发放测试道具。用法: GM.GiveItem \"Item Name\" <Count>"),
    [](const TArray<FString>& Args) {
        if (Args.Num() < 2) {
            return;
        }
        const FString& ItemName = Args[0];
        int32 Count = 1;
        LexTryParseString(Count, *Args[1]);
        GiveItem(ItemName, Count);
    }
);
```

## 屏幕打印 `FGameDebugPrint`

```cpp
class GAMEDEBUG_API FGameDebugPrint final {
public:
    static constexpr uint64 InvalidKey = MAX_uint64;

    static void Print(
        const FString& Message,
        const FColor& Color = FColor::Cyan,
        float Duration = 2.0f,
        uint64 Key = InvalidKey
    );

    static void PrintChannel(
        FName Channel,
        const FString& Message,
        const FColor& Color = FColor::Yellow,
        float Duration = 2.0f
    );

    static void PrintWarning(const FString& Message, float Duration = 5.0f);
    static void PrintError(const FString& Message, float Duration = 5.0f);

    [[nodiscard]] static uint64 GetChannelKey(FName Channel) noexcept;
};
```

```cpp
#define GAME_DEBUG_PRINT(Msg, ...)
#define GAME_DEBUG_PRINT_COLOR(Color, Duration, Msg, ...)
#define GAME_DEBUG_PRINT_CHANNEL(Channel, Msg, ...)
#define GAME_DEBUG_PRINT_WARN(Msg, ...)
#define GAME_DEBUG_PRINT_ERROR(Msg, ...)
```

| 接口 | 行为 |
| :--- | :--- |
| `Print` | `Key == InvalidKey` 追加新行，其余值原地刷新；负 / 非有限 `Duration` 钳制为 `0.0f` |
| `PrintChannel` | 经 64 位独立域盐键原地刷新，前缀 `[Channel] `；与 `WatchHub` 槽位正交 |
| `PrintWarning` / `PrintError` | 橙色 / 红色，默认驻留 5 秒 |
| `GetChannelKey` | 计算 64 位槽位键；`NAME_None` 返回 `InvalidKey` |

- **线程**：屏幕消息依赖引擎 GameThread 侧 API。
- **生命周期**：纯静态工具类，不可实例化。
- **失败语义**：非法时长被钳制，不报错；编译开关关闭时宏空操作。

```cpp
GAME_DEBUG_PRINT_CHANNEL(FName(TEXT("Player")), "Taken Damage: %.1f", Amount);
GAME_DEBUG_PRINT_WARN("Pending Tasks: %d", PendingTaskCount);
```

## 3D 绘制 `FGameDebugDraw`

<details>
<summary><b>展开 9 个绘制接口完整签名</b></summary>

```cpp
class GAMEDEBUG_API FGameDebugDraw final {
public:
    static void DrawLine(const UWorld* World, const FVector& LineStart, const FVector& LineEnd,
        const FColor& Color = FColor::Green, float Duration = 0.0f, float Thickness = 1.0f);

    static void DrawSphere(const UWorld* World, const FVector& Center, float Radius,
        const FColor& Color = FColor::Cyan, int32 Segments = 16, float Duration = 0.0f, float Thickness = 1.0f);

    static void DrawBox(const UWorld* World, const FVector& Center, const FVector& Extent,
        const FQuat& Rotation = FQuat::Identity, const FColor& Color = FColor::Yellow,
        float Duration = 0.0f, float Thickness = 1.0f);

    static void DrawCapsule(const UWorld* World, const FVector& Center, float HalfHeight, float Radius,
        const FQuat& Rotation = FQuat::Identity, const FColor& Color = FColor::Green,
        float Duration = 0.0f, float Thickness = 1.0f);

    static void DrawCylinder(const UWorld* World, const FVector& Start, const FVector& End, float Radius,
        int32 Segments = 16, const FColor& Color = FColor::Orange, float Duration = 0.0f, float Thickness = 1.0f);

    static void DrawCone(const UWorld* World, const FVector& Origin, const FVector& Direction, float Length,
        float AngleWidthRad, float AngleHeightRad, int32 NumSides = 16, const FColor& Color = FColor::Yellow,
        float Duration = 0.0f, float Thickness = 1.0f);

    static void DrawArrow(const UWorld* World, const FVector& LineStart, const FVector& LineEnd,
        float ArrowSize = 20.0f, const FColor& Color = FColor::Purple,
        float Duration = 0.0f, float Thickness = 1.0f);

    static void DrawCoordinateSystem(const UWorld* World, const FVector& AxisLoc, const FRotator& AxisRot,
        float Scale = 50.0f, float Duration = 0.0f, float Thickness = 1.0f);

    static void DrawString(const UWorld* World, const FVector& TextLocation, const FString& Text,
        AActor* TestActor = nullptr, const FColor& TextColor = FColor::White,
        float Duration = 0.0f, bool bDrawShadow = false, float FontScale = 1.0f);
};
```

</details>

- **宏**：`GAME_DEBUG_DRAW_LINE`、`_SPHERE`、`_BOX`、`_CAPSULE`、`_CYLINDER`、`_CONE`、`_ARROW`、`_COORD`、`_STRING`，全部 `do { ... } while (false)` 包裹。
- **默认色**：线 / 胶囊绿，球青，盒 / 锥黄，圆柱橙，箭头紫，坐标轴 X 红 / Y 绿 / Z 蓝，文本白。
- **防卫**：自动拦截 `World == nullptr`、`NaN` / `Inf`、负 `Duration` / `Thickness`、非正几何尺度、非法分段数、起终点重合、零体积盒与跨世界 / 失效 `TestActor`。
- **旋转**：`DrawBox` / `DrawCapsule` 对非单位 `FQuat` 自动安全归一化。
- **单位**：`DrawCone` 的张角参数为弧度；蓝图版本使用角度。
- **线程**：GameThread。
- **失败语义**：非法输入静默跳过，不抛异常。

```cpp
GAME_DEBUG_DRAW_LINE(GetWorld(), GetActorLocation(), GetActorLocation() + GetActorForwardVector() * 300.0f);
GAME_DEBUG_DRAW_SPHERE(GetWorld(), GetActorLocation(), 120.0f);
```

## 轨迹记录 `FGameDebugTrailRecorder`

```cpp
class GAMEDEBUG_API FGameDebugTrailRecorder final {
public:
    struct FTrailSample {
        FVector Location = FVector::ZeroVector;
        double Timestamp = 0.0;
    };

    static constexpr double UsePlatformClockSentinel = -1.0;

    explicit FGameDebugTrailRecorder(
        int32 InMaxSamples = 64,
        float InLifetimeSeconds = 2.0f,
        float InMinDistance = 10.0f
    );

    bool AddSample(const FVector& NewLocation, double CurrentTimeSeconds = UsePlatformClockSentinel);

    void DrawTrail(
        const UWorld* World,
        const FColor& HeadColor = FColor::Green,
        const FColor& TailColor = FColor::Blue,
        float Thickness = 2.0f,
        float Duration = 0.0f,
        double CurrentTimeSeconds = UsePlatformClockSentinel
    );

    void Clear() noexcept;
    [[nodiscard]] int32 GetSampleCount() const noexcept;
    [[nodiscard]] const FTrailSample& GetSample(int32 Index) const;
};
```

- **构造**：`MaxSamples` 下限 `2`，`LifetimeSeconds` 下限 `0.1f`，`MinDistance` 下限 `0.0f`（`0.0` 表示不做距离去重）。
- **采样**：`AddSample` 拦截 `NaN` / 非有限时间戳；`CurrentTimeSeconds >= 0.0`（含 `0.0`）直接采用，并对回退时间戳做单调非递减钳制；先 O(1) 清理超期点，再做距离去重与环形写入。
- **绘制**：`DrawTrail` 先淘汰超期样本，校验 `Thickness` / `Duration`，再按剩余生命周期做颜色渐变。
- **线程**：值语义，单实例不做内部同步，建议单线程使用。
- **生命周期**：构造期一次性分配定长缓冲，稳态零 `memmove`、零堆分配。
- **失败语义**：非法坐标 / 时间戳被拦截，不产生新采样。

```cpp
void AMyProjectile::Tick(float DeltaSeconds) {
    TrailRecorder.AddSample(GetActorLocation());
    TrailRecorder.DrawTrail(GetWorld(), FColor::Green, FColor::Blue, 2.0f, 0.0f);
}
```

## 时间控制 `FGameDebugTimeController`

```cpp
class GAMEDEBUG_API FGameDebugTimeController final {
public:
    static constexpr float MinTimeDilation = 0.001f;
    static constexpr float MaxTimeDilation = 20.0f;
    static constexpr float PausedTimeDilation = 0.0001f;
    static constexpr float PauseDetectionThreshold = 0.0005f;

    static void Initialize();
    static void Shutdown();

    static bool SetTimeDilation(const UObject* WorldContextObject, float TimeDilation);
    [[nodiscard]] static float GetTimeDilation(const UObject* WorldContextObject);
    static bool Pause(const UObject* WorldContextObject);
    static bool Resume(const UObject* WorldContextObject);
    [[nodiscard]] static bool IsPaused(const UObject* WorldContextObject);
    static bool ResetTime(const UObject* WorldContextObject);
    static bool StepFrame(const UObject* WorldContextObject, int32 Frames = 1);
};
```

| 接口 | 参数 | 行为 |
| :--- | :--- | :--- |
| `Initialize` / `Shutdown` | 无 | 对称注册 / 注销 `GM.TimeScale`、`GM.TimePause`、`GM.TimeResume`、`GM.TimeReset`、`GM.TimeStep` 与步进 Ticker |
| `SetTimeDilation` | 世界上下文、倍率 | 拦截 `NaN` / `Inf`，钳制至 `[0.001f, 20.0f]` |
| `GetTimeDilation` | 世界上下文 | 无效世界或开关关闭返回 `1.0f` |
| `Pause` | 世界上下文 | 写入 `0.0001f` 并记忆暂停前倍率；`MinGlobalTimeDilation` 阻止降至阈值以下时记 `Warning` 并返回 `false` |
| `Resume` / `ResetTime` | 世界上下文 | 恢复记忆倍率 / 强制重置 `1.0f` |
| `IsPaused` | 世界上下文 | 倍率 `<= 0.0005f` 判定为暂停 |
| `StepFrame` | 世界上下文、帧数 | 帧数下限钳制为 1；GM 指令解析拒绝非正帧数与尾随杂字符；补偿 `UWorld::Tick` 中途调用；世界销毁时回收状态 |

- **线程**：直接操作 `AWorldSettings::SetTimeDilation`，必须在 GameThread。
- **生命周期**：状态按 `UWorld`（`FObjectKey`）隔离，支持多 PIE。
- **失败语义**：世界上下文为空时按活跃 GameWorld 兜底；非空但无效的上下文直接返回失败，不污染无关世界。

```cpp
FGameDebugTimeController::SetTimeDilation(this, 0.2f);
FGameDebugTimeController::Pause(this);
FGameDebugTimeController::StepFrame(this, 1);
```

## 观察中心 `FGameDebugWatchHub`

```cpp
class GAMEDEBUG_API FGameDebugWatchHub final {
public:
    using FValueGetter = TFunction<FString()>;

    void Initialize();
    void Shutdown();

    bool Watch(FName Category, FName Name, FValueGetter Getter);
    bool Unwatch(FName Category, FName Name);

    void SetCategoryEnabled(FName Category, bool bEnabled);
    [[nodiscard]] bool IsCategoryEnabled(FName Category) const;
    void SetGlobalEnabled(bool bEnabled);
    [[nodiscard]] bool IsGlobalEnabled() const noexcept;

    void GetCategories(TArray<FName>& OutCategories) const;
    [[nodiscard]] int32 GetWatchItemCount(FName Category) const;
    void ClearAll();
    void TickDisplay(float DeltaTime);

    [[nodiscard]] static FGameDebugWatchHub& Get();
};
```

```cpp
#define GAME_DEBUG_WATCH(Category, Name, Getter)
#define GAME_DEBUG_UNWATCH(Category, Name)
```

| 接口 | 行为 |
| :--- | :--- |
| `Watch` | 同分类同名原地替换闭包；无效参数或开关关闭返回 `false` |
| `Unwatch` | 移除观察项；分类变空时即时撤销屏幕面板，默认开启的空分类自动回收 |
| `SetCategoryEnabled` / `IsCategoryEnabled` | 分类显示开关；关闭时即时撤销面板 |
| `SetGlobalEnabled` / `IsGlobalEnabled` | 原子总开关；关闭时撤销全部面板 |
| `GetCategories` / `GetWatchItemCount` / `ClearAll` | 分类与项数查询 / 清空 |
| `TickDisplay` | 每帧锁外快照已启用分类并原地刷新，驻留时长自适应 `DeltaTime` |

- **GM 指令**：`GM.Watch` 支持 `list`、`all <on/off/1/0/true/false>`、`<Category> <on/off/1/0/true/false>`，拦截空引号分类名。
- **线程**：`bGlobalEnabled` 为原子变量，注册表由 `FRWLock` 保护；`TickDisplay` 在 GameThread。
- **生命周期**：模块启动 / 卸载时对称调用 `Initialize()` / `Shutdown()`。
- **失败语义**：注册失败返回 `false`；闭包为空或名称无效不登记。

```cpp
TWeakObjectPtr<AMyCharacter> WeakSelf(this);
GAME_DEBUG_WATCH("Player", "Speed", [WeakSelf]() {
    if (const AMyCharacter* Self = WeakSelf.Get()) {
        return FString::Printf(TEXT("%.1f cm/s"), Self->GetVelocity().Size());
    }
    return FString(TEXT("Destroyed"));
});

GAME_DEBUG_UNWATCH("Player", "Speed");
```

## 作用域秒表 `FScopedDebugStopwatch`

```cpp
class GAMEDEBUG_API FScopedDebugStopwatch final {
public:
    static constexpr double SevereThresholdMultiplier = 2.0;
    static constexpr float AlertDisplayDurationSeconds = 3.0f;

    explicit FScopedDebugStopwatch(
        FName InScopeName,
        double InThresholdMs = 0.0,
        FName InChannel = FName(TEXT("Profiler"))
    );

    ~FScopedDebugStopwatch();

    [[nodiscard]] double GetElapsedMs() const noexcept;
};
```

```cpp
#define GAME_DEBUG_SCOPED_TIMER(ScopeName, ThresholdMs)
#define GAME_DEBUG_SCOPED_TIMER_CHANNEL(ScopeName, ThresholdMs, Channel)
```

- **构造**：使用 `FScopedCpuTimer` 记录起始硬件周期；`InThresholdMs <= 0.0` 或非有限值归零，`0.0` 表示总是告警。
- **析构**：耗时 `>= ThresholdMs` 时经 `LogGameDebug` 与屏幕通道告警；`>= 2x` 阈值为红色，普通超阈为橙色。
- **裁剪**：`GAME_ENABLE_CPU_PROFILER == 0` 时全部成员与时钟运算移除；`GetElapsedMs()` 恒返回 `0.0`。
- **宏**：基于 `__COUNTER__` 生成唯一变量名，防同行冲突。
- **线程**：计时本身无共享状态；屏幕告警依赖 GameThread。
- **失败语义**：阈值非法归零，不抛异常。

```cpp
void AMyProjectile::Tick(float DeltaSeconds) {
    GAME_DEBUG_SCOPED_TIMER_CHANNEL("ProjectileTick", 0.5, "CombatPerf");
    Super::Tick(DeltaSeconds);
}
```

## 蓝图函数库 `UGameDebugBlueprintLibrary`

<details>
<summary><b>展开 4 个打印、9 个绘制与 7 个时间节点签名</b></summary>

```cpp
UFUNCTION(BlueprintCallable, Category = "GameDebug|Print")
static void PrintDebugMessage(const FString& Message, FColor Color = FColor::Cyan, float Duration = 2.0f, int64 Key = -1);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Print")
static void PrintChannelMessage(FName Channel, const FString& Message, FColor Color = FColor::Yellow, float Duration = 2.0f);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Print")
static void PrintWarning(const FString& Message, float Duration = 5.0f);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Print")
static void PrintError(const FString& Message, float Duration = 5.0f);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
static void DrawDebugLine(const UObject* WorldContextObject, FVector LineStart, FVector LineEnd,
    FColor Color = FColor::Green, float Duration = 0.0f, float Thickness = 1.0f);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
static void DrawDebugSphere(const UObject* WorldContextObject, FVector Center, float Radius = 50.0f,
    FColor Color = FColor::Cyan, int32 Segments = 16, float Duration = 0.0f, float Thickness = 1.0f);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
static void DrawDebugBox(const UObject* WorldContextObject, FVector Center, FVector Extent = FVector(50.0, 50.0, 50.0),
    FRotator Rotation = FRotator::ZeroRotator, FColor Color = FColor::Yellow, float Duration = 0.0f, float Thickness = 1.0f);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
static void DrawDebugCapsule(const UObject* WorldContextObject, FVector Center, float HalfHeight = 88.0f,
    float Radius = 34.0f, FRotator Rotation = FRotator::ZeroRotator, FColor Color = FColor::Green,
    float Duration = 0.0f, float Thickness = 1.0f);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
static void DrawDebugCylinder(const UObject* WorldContextObject, FVector Start, FVector End, float Radius = 50.0f,
    int32 Segments = 16, FColor Color = FColor(243, 156, 18, 255), float Duration = 0.0f, float Thickness = 1.0f);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
static void DrawDebugCone(const UObject* WorldContextObject, FVector Origin, FVector Direction, float Length = 200.0f,
    float AngleWidthDeg = 45.0f, float AngleHeightDeg = 45.0f, int32 NumSides = 16,
    FColor Color = FColor::Yellow, float Duration = 0.0f, float Thickness = 1.0f);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
static void DrawDebugArrow(const UObject* WorldContextObject, FVector LineStart, FVector LineEnd,
    float ArrowSize = 20.0f, FColor Color = FColor(169, 7, 228, 255), float Duration = 0.0f, float Thickness = 1.0f);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
static void DrawDebugCoordinateSystem(const UObject* WorldContextObject, FVector AxisLoc, FRotator AxisRot,
    float Scale = 50.0f, float Duration = 0.0f, float Thickness = 1.0f);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Draw", meta = (WorldContext = "WorldContextObject"))
static void DrawDebugString(const UObject* WorldContextObject, FVector TextLocation, const FString& Text,
    FColor TextColor = FColor::White, float Duration = 0.0f, bool bDrawShadow = false, float FontScale = 1.0f);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Time", meta = (WorldContext = "WorldContextObject"))
static bool SetGlobalTimeDilation(const UObject* WorldContextObject, float TimeDilation);

UFUNCTION(BlueprintPure, Category = "GameDebug|Time", meta = (WorldContext = "WorldContextObject"))
static float GetGlobalTimeDilation(const UObject* WorldContextObject);

UFUNCTION(BlueprintPure, Category = "GameDebug|Time", meta = (WorldContext = "WorldContextObject"))
static bool IsGamePaused(const UObject* WorldContextObject);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Time", meta = (WorldContext = "WorldContextObject"))
static bool PauseGameTime(const UObject* WorldContextObject);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Time", meta = (WorldContext = "WorldContextObject"))
static bool ResumeGameTime(const UObject* WorldContextObject);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Time", meta = (WorldContext = "WorldContextObject"))
static bool ResetGameTime(const UObject* WorldContextObject);

UFUNCTION(BlueprintCallable, Category = "GameDebug|Time", meta = (WorldContext = "WorldContextObject"))
static bool StepGameFrames(const UObject* WorldContextObject, int32 Frames = 1);
```

</details>

- **蓝图口默认值**：`DrawDebugCylinder` 为橙色 `FColor(243, 156, 18, 255)`，`DrawDebugArrow` 为紫色 `FColor(169, 7, 228, 255)`，与 C++ 侧对齐。
- **角度单位**：蓝图 `DrawDebugCone` 使用角度，C++ `FGameDebugDraw::DrawCone` 使用弧度。
- **线程**：蓝图节点由 GameThread 调用。
- **生命周期**：纯静态函数库，无实例状态。
- **失败语义**：世界上下文无效时绘制 / 时间操作静默失败或返回 `false`。

## 常见错误与边界

- **Shipping 裁剪**：`GAME_DEBUG_PRINT*` / `GAME_DEBUG_WATCH*` / `GAME_DEBUG_DRAW_*` 在开关关闭时展开为 `((void)0)`，实参不求值；直接调用函数仍会构造实参，只是函数体短路。
- **观察闭包寿命**：`Watch` 的闭包捕获 `UObject` / `AActor` 时必须用 `TWeakObjectPtr`，并在 `EndPlay` 对称 `Unwatch`。
- **暂停判定**：`IsPaused` 以 `<= 0.0005f` 为阈值；`0.001f` 不会被视为暂停。`MinGlobalTimeDilation` 可能阻止写入暂停阈值。
- **GM 名称**：`RegisterCheat` 拒绝 `NAME_None`、含空白字符的名称、空回调和重名。
- **轨迹容量**：容量在构造期固定（默认 64），运行期不可扩容；生命周期与最小距离有下限钳制。
- **绘制输入**：非有限坐标、非正半径 / 半高 / 张角 / 字体缩放、非法分段数与起终点重合会被静默跳过。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [快速上手指南](../Guides/QuickStart.md)
