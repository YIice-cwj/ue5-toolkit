# GameFeedback 表现信号与原子动作参数 API 参考

本文档列出 `GameFeedback` 插件中定义的表现纯数据载荷与 6 组原子动作参数。

- 头文件：
  - `#include "Feedback/GameFeedbackSignal.h"`
  - `#include "Feedback/GameFeedbackActions.h"`

---

## 1. 纯表现信号 (`FGameFeedbackSignal`)

`FGameFeedbackSignal` 是战斗逻辑与视听表现层交互的**唯一通用数据载荷**（蓝图可读写）。

```cpp
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackSignal {
    GENERATED_BODY()

    /** 动作发起者实体（如玩家 Pawn） */
    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TWeakObjectPtr<AActor> Instigator = nullptr;

    /** 伤害造成者实体（如子弹、投掷物或武器 Actor） */
    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TWeakObjectPtr<AActor> DamageCauser = nullptr;

    /** 受击目标实体 */
    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TWeakObjectPtr<AActor> Target = nullptr;

    /** 碰撞命中信息（包含 ImpactPoint、ImpactNormal、BoneName、Component 等） */
    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FHitResult HitResult;

    /** 冲击作用方向（通常由子弹速度或击退朝向决定） */
    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FVector ImpactDirection = FVector::ZeroVector;

    /** 弹道扫描轨迹方向（TraceEnd - TraceStart 的单位化向量） */
    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FVector TraceDirection = FVector::ZeroVector;

    /** 主伤害通道标签（如 Damage.Physical、Damage.Fire），用于注册表第一层快速路由 */
    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FGameplayTag PrimaryChannel;

    /** 表现特征标签集合（如 Feedback.State.Lethal、Feedback.Trait.Critical、Feedback.HeavyHit） */
    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FGameplayTagContainer FeedbackTags;

    /** 表现强度数值（通常为伤害总值），低于原子动作 MinMagnitude 时将被对应动作静默跳过 */
    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    float Magnitude = 0.0f;

    /** 是否致死打击（若为 true 且配置了 bGlobalFreezeOnLethal，可触发全局特写停顿） */
    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    bool bLethal = false;

    /** 跨端预测事件 ID（0 表示不参与预测去重） */
    UPROPERTY()
    uint16 PredictionEventId = 0;
};
```

### 1.1 视角枚举与致死标签
- **`EGameFeedbackPerspective`**：
  - `LocalAttacker`：本地玩家是本次打击的发起者/造成者；
  - `LocalVictim`：本地玩家是本次打击的受害者；
  - `Bystander`：本地玩家是旁观者（免除镜头震动与触觉震动）。
- **`TAG_Feedback_State_Lethal`**：
  - 常量原生标签 `"Feedback.State.Lethal"`，当且仅当伤害造成击杀时注入。

---

## 2. 原子动作参数聚合 (`GameFeedbackActions.h`)

表现配置由 6 组完全解耦的原子参数构成，每组参数专管一种硬件或引擎管线表现：

### 2.1 打击顿帧 (`FGameFeedbackHitStopAction`)
控制物理定格与局部时间膨胀：
- `bool bEnable = true`：是否启用顿帧；
- `float VictimDuration = 0.08f`：受击者定格时长（秒）；
- `float AttackerDuration = 0.04f`：攻击者定格时长（秒，通常略小于受击者以优化攻速手感）；
- `float TimeDilation = 0.001f`：定格期间的时间膨胀率（接近 0）；
- `float CooldownInterval = 0.05f`：同攻击者对同目标的触发冷却间隔（秒），避免多段高频伤害瞬间把角色卡死；
- `float MaxCumulativeDuration = 0.20f`：滑动窗口内最大累积定格上限（秒）；
- `float ResetWindow = 0.50f`：累积预算清零重置窗口（秒）；
- `bool bGlobalFreezeOnLethal = false`：致死打击时是否触发全局微缩时间特写。

### 2.2 镜头震动 (`FGameFeedbackCameraShakeAction`)
驱动基于 PlayerCameraManager 的镜头震屏：
- `TSubclassOf<UCameraShakeBase> AttackerShakeClass`：攻击者镜头震动类；
- `float AttackerShakeScale = 1.0f`：攻击者震动倍率；
- `TSubclassOf<UCameraShakeBase> VictimShakeClass`：受击者镜头震动类；
- `float VictimShakeScale = 1.5f`：受击者震动倍率；
- `float MinMagnitude = 5.0f`：触发镜头震动的最小强度门槛；
- `bool bOrientShakeToImpactDirection = true`：是否按相对局部冲击方向旋转震动空间。

### 2.3 材质受击闪白 (`FGameFeedbackHitFlashAction`)
通过 Overlay Material 表现受击泛白：
- `bool bEnable = true`：是否启用材质闪白；
- `EGameFeedbackFlashTargetMode TargetMode`：
  - `HitComponentOnly`：仅闪烁被命中的具体网格体；
  - `AllSkeletalMeshes`：闪烁目标身上的全部骨骼网格体（常用）；
  - `TaggedComponentsOnly`：仅闪烁带有指定 Tag 的组件；
- `FName ComponentFilterTag = TEXT("Flashable")`：针对 Tag 模式的组件筛选标签；
- `TObjectPtr<UMaterialInterface> OverlayMaterial = nullptr`：叠加的闪白材质；
- `float Duration = 0.10f`：闪白持续时长（秒）；
- `float MinMagnitude = 3.0f`：触发闪白的最小强度门槛。

### 2.4 粒子特效 (`FGameFeedbackVFXAction`)
世界空间或附着生成 Niagara 粒子：
- `TSoftObjectPtr<UNiagaraSystem> NiagaraSystem`：粒子资产软引用（按需异步预热）；
- `bool bAttachToHitComponent = false`：是否挂接至命中骨骼/组件（移动目标适用）；
- `EGameFeedbackVFXOrientation OrientationMode`：朝向计算模式：
  - `AlignToNormal`：对齐命中表面法线；
  - `AlignToImpactDirection`：对齐显式冲击方向；
  - `OppositeToTraceDirection`：弹道入射反方向；
  - `AlignToReflectedNormal`：弹道相对法线的反射向量；
- `float MinMagnitude = 1.0f`：触发特效的最小强度门槛。

### 2.5 音效 (`FGameFeedbackAudioAction`)
空间 3D 音效与攻击者 2D 命中确认音：
- `TSoftObjectPtr<USoundBase> SpatialImpactSound`：世界空间受击音效软引用；
- `TObjectPtr<USoundAttenuation> AttenuationSettings`：空间衰减配置；
- `TObjectPtr<USoundConcurrency> ConcurrencySettings`：并发限制配置；
- `TSoftObjectPtr<USoundBase> AttackerConfirmationSound`：仅攻击者能听见的 2D 击中音（Hit Marker 音）；
- `float MinMagnitude = 1.0f`：触发音效的最小强度门槛。

### 2.6 手柄触觉震动 (`FGameFeedbackForceFeedbackAction`)
支持现代手柄触觉力反馈：
- `TObjectPtr<UHapticFeedbackEffect_Base> AttackerHaptic`：攻击者震动反馈；
- `TObjectPtr<UHapticFeedbackEffect_Base> VictimHaptic`：受击者震动反馈；
- `float MinMagnitude = 5.0f`：触发手柄震动的最小强度门槛；
- 震动强度受项目全局配置 `UGameFeedbackSettings::GlobalHapticScale` 整体缩放。
