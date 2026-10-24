# GameCore 时间膨胀与顿帧 API 参考

`UTimeDilationSubsystem` 是一个世界子系统（`UTickableWorldSubsystem`），负责统一控制 Actor 局部受击停顿（HitStop）与全局分层时间缩放（Bullet Time / SlowMo）。

头文件：
- `#include "Time/TimeDilationSubsystem.h"`
- `#include "Time/TimeDilationTypes.h"`

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
class GAMECORE_API UTimeDilationSubsystem : public UTickableWorldSubsystem {
    GENERATED_BODY()

public:
    static UTimeDilationSubsystem& Get(const UObject* WorldContextObject);
    static UTimeDilationSubsystem* GetPtr(const UObject* WorldContextObject);

    /**
     * @brief 请求对单个 Actor 施加打击顿帧
     * @param Target 目标 Actor
     * @param Duration 顿帧持续真实时间（秒）
     * @param TimeDilation 顿帧期间局部时间缩放（默认 0.001f）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Time")
    void RequestHitStop(AActor* Target, float Duration, float TimeDilation = 0.001f);

    /**
     * @brief 清除指定 Actor 的打击顿帧并立即还原时间缩放
     * @param Target 目标 Actor
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Time")
    void ClearHitStop(AActor* Target);

    /**
     * @brief 检查指定 Actor 当前是否正处于打击顿帧中
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Time")
    bool IsActorInHitStop(const AActor* Target) const;

    /**
     * @brief 清除所有 Actor 的打击顿帧
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Time")
    void ClearAllHitStops();

    /**
     * @brief 在指定通道请求全局时间膨胀
     * @param Channel 目标通道
     * @param Duration 持续时间（秒，<=0 表示持久有效直至手动清除）
     * @param TimeDilation 目标时间缩放（如 0.2f 为 5 倍慢动作）
     * @param BlendOutDuration 结束时的平滑淡出时间（秒）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Time")
    void RequestGlobalDilation(ETimeDilationChannel Channel, float Duration, float TimeDilation, float BlendOutDuration = 0.0f);

    /**
     * @brief 清除指定通道的全局时间膨胀（瞬间重置为 1.0f）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Time")
    void ClearGlobalDilation(ETimeDilationChannel Channel);

    /**
     * @brief 清除指定通道的全局时间膨胀，并支持平滑淡出恢复
     * @param Channel 目标通道
     * @param BlendOutDuration 淡出过渡时间（秒）
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Time")
    void ClearGlobalDilationWithBlend(ETimeDilationChannel Channel, float BlendOutDuration = 0.2f);

    /**
     * @brief 清除所有通道的全局时间膨胀
     */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Time")
    void ClearAllGlobalDilations();

    /**
     * @brief 获取当前生效的最终全局时间缩放值
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Time")
    float GetEffectiveGlobalTimeDilation() const;

    /**
     * @brief 获取指定通道当前的激活状态
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Time")
    bool IsChannelActive(ETimeDilationChannel Channel) const;

    /**
     * @brief 获取指定通道当前的时间缩放值
     */
    UFUNCTION(BlueprintPure, Category = "GameCore|Time")
    float GetChannelTimeDilation(ETimeDilationChannel Channel) const;
};
```

---

## 3. 运行逻辑与生命周期

1. **局部 HitStop 恢复机制**：
   - 施加 HitStop 时，暂存该 Actor 原始的 `CustomTimeDilation`，并临时设置为 `TimeDilation`；
   - 子系统在内部维护弱引用映射表，随真实时钟推进剩余时长；到期后恢复原有的 `CustomTimeDilation`；若 Actor 在顿帧期间意外销毁，弱引用在下一次扫描时自动回收。
2. **多通道优先级覆盖**：
   - 通道按枚举序号决定优先级（`SystemPause > HitStop > SlowMo > WorldModifier`）；
   - 生效时取首个处于激活状态的高优先级通道数值作为最终写入 `AWorldSettings::TimeDilation` 的值；
   - 支持平滑淡出：到期时或通过 `ClearGlobalDilationWithBlend` 清除时，以线性插值由当前倍率平滑过渡至 1.0f。
