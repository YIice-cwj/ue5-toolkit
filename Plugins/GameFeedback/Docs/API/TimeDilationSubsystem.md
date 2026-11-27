# GameFeedback 时间膨胀与顿帧 API 参考

`UTimeDilationSubsystem` 是一个世界子系统（`UTickableWorldSubsystem`），仅在游戏世界创建并支持**空闲零 Tick 休眠**，负责统一控制 Actor 局部受击停顿（HitStop）与全局分层时间缩放（Bullet Time / SlowMo）。

头文件：
- `#include "Time/TimeDilationSubsystem.h"`
- `#include "Time/TimeDilationTypes.h"`
- `#include "Time/FActorHitStopTable.h"`
- `#include "Time/FGlobalDilationMixer.h"`

---

## 1. 核心枚举与通道优先级

### 1.1 `ETimeDilationChannel`
全局时间膨胀通道（枚举数值越小优先级越高，高优先级激活时直接覆盖低优先级）：
```cpp
UENUM(BlueprintType)
enum class ETimeDilationChannel : uint8 {
    SystemPause = 0,    // 系统级菜单暂停
    HitStop = 1,        // 全局顿帧 / 震屏定格
    SlowMo = 2,         // 全局慢动作（子弹时间）
    WorldModifier = 3,  // 世界环境与关卡机制修饰
    Count = 4
};
```

---

## 2. `UTimeDilationSubsystem` 类方法

```cpp
UCLASS()
class GAMEFEEDBACK_API UTimeDilationSubsystem : public UTickableWorldSubsystem {
    GENERATED_BODY()

public:
    static UTimeDilationSubsystem& Get(const UObject* WorldContextObject);
    static UTimeDilationSubsystem* GetPtr(const UObject* WorldContextObject);

    bool ShouldCreateSubsystem(UObject* Outer) const override;
    bool IsTickable() const override;

    UFUNCTION(BlueprintCallable, Category = "GameFeedback|Time")
    void RequestHitStop(AActor* Target, float Duration, float TimeDilation = 0.001f);

    UFUNCTION(BlueprintCallable, Category = "GameFeedback|Time")
    void ClearHitStop(AActor* Target);

    UFUNCTION(BlueprintPure, Category = "GameFeedback|Time")
    bool IsActorInHitStop(const AActor* Target) const;

    UFUNCTION(BlueprintCallable, Category = "GameFeedback|Time")
    void ClearAllHitStops();

    /** TimeDilation 钳制至 [0.0001, 20]，Duration <= 0 表示持久有效直至手动清除 */
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

---

## 3. 运行逻辑与性能特性

1. **空闲零 Tick 休眠与非游戏世界隔离**：
   - `ShouldCreateSubsystem` 过滤编辑器与预览世界；
   - `IsTickable()` 仅当存在活跃 Actor 顿帧或全局膨胀通道且非 CDO 时返回 `true`，空闲期零 Tick 开销；从空闲态唤醒时自动同步刷新 `LastRealTime` 防止首帧时间突跳。
2. **静态零堆分配与安全钳制**：
   - `FGlobalDilationMixer` 使用 `TStaticArray<FGlobalChannelState, 4>` 静态内联存储，零堆分配，所有时间缩放倍率统一安全钳制至 `[0.0001, 20.0]`；
   - `FActorHitStopTable` 统一基于不可变 `TObjectKey<AActor>` 与 `IsValid(ResolveObjectPtr())` 管理局部顿帧与销毁自愈。
3. **日志分级**：
   - 高频请求/恢复路径通过 `GameLogging`（`LogGameFeedback`）以 `[TimeDilation]` 标签输出 `Verbose` 级限频日志，默认静默且零时钟/零 CAS 开销。
