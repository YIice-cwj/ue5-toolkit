# GameFeedback 分层时间膨胀与顿帧 API 参考

> **一句话**：Actor 局部顿帧与全局分层时间缩放的公共 API。
> **头文件**：`Time/TimeDilationSubsystem.h`、`Time/TimeDilationTypes.h`、`Time/FActorHitStopTable.h`、`Time/FGlobalDilationMixer.h` · **线程**：仅 GameThread；Tick 与世界时间写回均在 GameThread · **失败语义**：无效 Actor、非正 `Duration`、NaN 请求静默忽略；未激活通道返回 `1.0f`

## 30 秒速览

- **通道**：`ETimeDilationChannel` 数值越小优先级越高，高优先级激活时覆盖低优先级。
- **局部顿帧**：`RequestHitStop` / `ClearHitStop` / `IsActorInHitStop` / `ClearAllHitStops`。
- **全局膨胀**：`RequestGlobalDilation` / `ClearGlobalDilation` / `ClearGlobalDilationWithBlend` / `ClearAllGlobalDilations`。
- **约束**：时间缩放统一钳制到 `[0.0001, 20]`；`Duration <= 0` 表示持久有效，需手动清除。
- **休眠**：`IsTickable()` 仅在存在活跃顿帧或全局通道且非 CDO 时返回 `true`，空闲零 Tick。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UTimeDilationSubsystem::GetPtr` | 获取时间膨胀子系统 | 仅 GameThread；未就绪返回 `nullptr` |
| `UTimeDilationSubsystem::Get` | 获取子系统引用 | 服务不存在时断言，仅确定存在时使用 |
| `UTimeDilationSubsystem::RequestHitStop` | 对 Actor 施加局部顿帧 | 非正 / NaN `Duration` 或无效 `Target` 不生效 |
| `UTimeDilationSubsystem::ClearHitStop` | 立即还原该 Actor 时间缩放 | 仅 GameThread |
| `UTimeDilationSubsystem::IsActorInHitStop` | 查询 Actor 是否处于顿帧 | 纯查询，无副作用 |
| `UTimeDilationSubsystem::RequestGlobalDilation` | 按通道施加全局时间缩放 | `Duration <= 0` 持久有效；缩放钳制 `[0.0001, 20]` |
| `UTimeDilationSubsystem::ClearGlobalDilation` | 立即清除指定通道 | 持久通道须显式清除 |
| `UTimeDilationSubsystem::ClearAllGlobalDilations` | 清除全部全局通道 | 仅 GameThread |
| `UTimeDilationSubsystem::GetEffectiveGlobalTimeDilation` | 查当前最终全局缩放 | 仅在 GameThread 读取 |
| `UTimeDilationSubsystem::GetChannelTimeDilation` | 查指定通道缩放 | 未激活返回 `1.0f` |

## 通道优先级

```cpp
UENUM(BlueprintType)
enum class ETimeDilationChannel : uint8 {
    SystemPause = 0,
    HitStop = 1,
    SlowMo = 2,
    WorldModifier = 3,
    Count = 4
};
```

| 枚举值 | 数值 | 典型用途 |
| :--- | :---: | :--- |
| `SystemPause` | `0` | 系统级菜单暂停 |
| `HitStop` | `1` | 全局顿帧 / 震屏定格 |
| `SlowMo` | `2` | 全局慢动作（子弹时间） |
| `WorldModifier` | `3` | 世界环境与关卡机制修饰 |

## 子系统 `UTimeDilationSubsystem`

```cpp
UCLASS()
class GAMEFEEDBACK_API UTimeDilationSubsystem : public UTickableWorldSubsystem {
    GENERATED_BODY()

public:
    static UTimeDilationSubsystem& Get(const UObject* WorldContextObject);
    [[nodiscard]] static UTimeDilationSubsystem* GetPtr(const UObject* WorldContextObject);

    bool ShouldCreateSubsystem(UObject* Outer) const override;
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;
    void Tick(float DeltaTime) override;
    TStatId GetStatId() const override;
    bool IsTickable() const override;
    bool IsTickableWhenPaused() const override;

    UFUNCTION(BlueprintCallable, Category = "GameFeedback|Time")
    void RequestHitStop(AActor* Target, float Duration, float TimeDilation = 0.001f);

    UFUNCTION(BlueprintCallable, Category = "GameFeedback|Time")
    void ClearHitStop(AActor* Target);

    UFUNCTION(BlueprintPure, Category = "GameFeedback|Time")
    bool IsActorInHitStop(const AActor* Target) const;

    UFUNCTION(BlueprintCallable, Category = "GameFeedback|Time")
    void ClearAllHitStops();

    UFUNCTION(BlueprintCallable, Category = "GameFeedback|Time")
    void RequestGlobalDilation(ETimeDilationChannel Channel, float Duration, float TimeDilation, float BlendOutDuration = 0.0f);

    UFUNCTION(BlueprintCallable, Category = "GameFeedback|Time")
    void ClearGlobalDilation(ETimeDilationChannel Channel);

    UFUNCTION(BlueprintCallable, Category = "GameFeedback|Time")
    void ClearGlobalDilationWithBlend(ETimeDilationChannel Channel, float BlendOutDuration = 0.2f);

    UFUNCTION(BlueprintCallable, Category = "GameFeedback|Time")
    void ClearAllGlobalDilations();

    UFUNCTION(BlueprintPure, Category = "GameFeedback|Time")
    float GetEffectiveGlobalTimeDilation() const;

    UFUNCTION(BlueprintPure, Category = "GameFeedback|Time")
    bool IsChannelActive(ETimeDilationChannel Channel) const;

    UFUNCTION(BlueprintPure, Category = "GameFeedback|Time")
    float GetChannelTimeDilation(ETimeDilationChannel Channel) const;

    void AdvanceTime(float RealDeltaSeconds);
};
```

### 获取服务

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `Get` | `WorldContextObject` | 子系统引用 | 断言有效；仅用于确定服务存在时 |
| `GetPtr` | `WorldContextObject` | 子系统指针 | 静默返回 `nullptr`，推荐常规调用 |

### Actor 局部顿帧

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `RequestHitStop` | `Target`、`Duration`、`TimeDilation` | `void` | 非正 / NaN `Duration` 或无效 `Target` 不生效；同一 Actor 重复请求取较大时长与较小缩放 |
| `ClearHitStop` | `Target` | `void` | 立即还原该 Actor 的原始时间缩放 |
| `IsActorInHitStop` | `Target` | `bool` | 目标处于顿帧中返回 `true` |
| `ClearAllHitStops` | 无 | `void` | 清除全部 Actor 顿帧并还原时间缩放 |

### 全局分层膨胀

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `RequestGlobalDilation` | `Channel`、`Duration`、`TimeDilation`、`BlendOutDuration` | `void` | 重复请求整体覆盖该通道；`Duration <= 0` 持久有效；`BlendOutDuration` 负数按 `0` 处理 |
| `ClearGlobalDilation` | `Channel` | `void` | 立即清除指定通道 |
| `ClearGlobalDilationWithBlend` | `Channel`、`BlendOutDuration` | `void` | 平滑淡出恢复；`<= 0` 等价于立即清除 |
| `ClearAllGlobalDilations` | 无 | `void` | 清除全部通道 |

### 查询

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `GetEffectiveGlobalTimeDilation` | 无 | `float` | 当前生效的最终全局时间缩放 |
| `IsChannelActive` | `Channel` | `bool` | 通道是否激活 |
| `GetChannelTimeDilation` | `Channel` | `float` | 通道当前缩放；未激活返回 `1.0f` |
| `AdvanceTime` | `RealDeltaSeconds` | `void` | 手动推进真实时间，供单测或自定义时钟驱动 |

```cpp
if (UTimeDilationSubsystem* Time = UTimeDilationSubsystem::GetPtr(this)) {
    Time->RequestHitStop(HitActor, 0.08f, 0.001f);
    Time->RequestGlobalDilation(ETimeDilationChannel::SlowMo, 1.5f, 0.3f, 0.2f);
}
```

## 运行逻辑与性能

- **世界隔离**：`ShouldCreateSubsystem` 过滤编辑器视口与预览世界，仅在游戏世界创建；Dedicated Server 同样创建，权威端顿帧请求仍受 `bEnableFeedback` 配置门禁控制。
- **空闲休眠**：`IsTickable()` 仅当存在活跃 Actor 顿帧或全局膨胀通道且非 CDO 时返回 `true`；从空闲唤醒时同步刷新 `LastRealTime`，避免首帧时间突跳。
- **暂停仍推进**：`IsTickableWhenPaused()` 恒为 `true`，暂停期间继续刷新真实时间基准。
- **零堆分配**：`FGlobalDilationMixer` 使用 `TStaticArray<FGlobalChannelState, 4>` 静态内联存储；`FActorHitStopTable` 以 `TObjectKey<AActor>` 与 `IsValid(ResolveObjectPtr())` 管理局部顿帧与销毁自愈。
- **跨阶段余量**：`FGlobalDilationMixer::Advance` 的单帧余量在同一通道内连续消费持续阶段与淡出阶段，不会因阶段切换丢弃剩余时间。
- **日志**：高频请求 / 恢复路径通过 `LogGameFeedback` 以 `[TimeDilation]` 标签输出 `Verbose` 级限频日志，默认静默且被抑制时零时钟、零 CAS 开销。

## 常见错误与边界

- **非正时长**：`RequestHitStop` 对 `Duration <= 0`、NaN 或无效 `Target` 直接忽略，不会留下半成品状态。
- **缩放钳制**：所有时间缩放统一钳制到 `[0.0001, 20]`，传入 `0` 或超大值不会生效为原始值。
- **持久通道**：`RequestGlobalDilation` 的 `Duration <= 0` 表示一直有效，必须显式 `ClearGlobalDilation` 或 `ClearAllGlobalDilations`。
- **优先级覆盖**：高优先级通道激活时会覆盖低优先级通道，低通道状态仍保留，直到高通道清除后重新生效。
- **服务获取**：`Get` 在服务不存在时断言；不确定世界类型时使用 `GetPtr`。
- **编辑器世界**：编辑器视口与预览世界不创建该子系统，相关调用应跳过。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [快速上手](../Guides/QuickStart.md)
