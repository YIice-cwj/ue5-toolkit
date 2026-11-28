# GameFeedback 表现信号与原子动作参数 API 参考

> **一句话**：战斗逻辑与表现层之间的纯信号载荷、6 组原子动作参数与紧凑网络载荷。
> **头文件**：`Feedback/GameFeedbackSignal.h`、`Feedback/GameFeedbackActions.h`、`Network/GameFeedbackEventNet.h` · **线程**：仅 GameThread；网络载荷在权威端构造并在客户端回灌 · **失败语义**：`NetSerialize` 失败返回 `false`；`Magnitude` 低于动作 `MinMagnitude` 时该动作静默跳过

## 30 秒速览

- **信号**：`FGameFeedbackSignal` 只承载表现所需字段，不包含伤害结算规则。
- **预测**：`PredictionEventId == 0` 表示不参与跨端预测去重。
- **动作**：HitStop、CameraShake、HitFlash、VFX、Audio、Haptics 六组参数完全解耦。
- **视角**：`EGameFeedbackPerspective` 区分本地攻击者、本地受害者与旁观者；致死标签为 `Feedback.State.Lethal`。
- **网络**：`FGameFeedbackEventNet` 采用量化向量、半精度伤害与位掩码，历史 Development 构建参考值典型 41 字节、完整 44 字节。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FGameFeedbackSignal` | 纯表现信号载荷 | 仅 GameThread 组装；弱指针不延长 Actor 生命周期 |
| `FGameFeedbackSignal::PredictionEventId` | 跨端预测去重 ID | 为 `0` 表示不参与预测去重 |
| `FGameFeedbackHitStopAction` | 打击顿帧参数 | `TimeDilation` 钳制 `[0.0001, 20]`；低于 `MinMagnitude` 跳过 |
| `FGameFeedbackVFXAction` | 粒子特效参数 | `NiagaraSystem` 为软引用，需经 Provider 预热 |
| `EGameFeedbackPerspective::Bystander` | 旁观者视角 | 免除镜头震动与触觉 |
| `FGameFeedbackEventNet::FromSignal` | 由信号构造紧凑报文 | 需传入权威 `DefinitionId` 与映射版本 |
| `FGameFeedbackEventNet::ToSignal` | 由紧凑报文还原信号 | 值类型载荷，可安全复制 |
| `FGameFeedbackEventNet::NetSerialize` | 逐字段序列化网络载荷 | 任一子字段失败即返回 `false` |
| `FGameFeedbackEventNet::SetDamage` | 半精度编码伤害值 | NaN 归零并钳制 `[0, 65504]` |

## 纯表现信号 `FGameFeedbackSignal`

```cpp
USTRUCT(BlueprintType)
struct GAMEFEEDBACK_API FGameFeedbackSignal {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TWeakObjectPtr<AActor> Instigator = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TWeakObjectPtr<AActor> DamageCauser = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    TWeakObjectPtr<AActor> Target = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FHitResult HitResult;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FVector ImpactDirection = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FVector TraceDirection = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FGameplayTag PrimaryChannel;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    FGameplayTagContainer FeedbackTags;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    float Magnitude = 0.0f;

    UPROPERTY(BlueprintReadWrite, Category = "GameFeedback")
    bool bLethal = false;

    UPROPERTY()
    uint16 PredictionEventId = 0;
};
```

| 字段 | 类型 | 语义 |
| :--- | :--- | :--- |
| `Instigator` | `TWeakObjectPtr<AActor>` | 动作发起者，如玩家 Pawn |
| `DamageCauser` | `TWeakObjectPtr<AActor>` | 伤害造成者，如子弹、投掷物或武器 |
| `Target` | `TWeakObjectPtr<AActor>` | 受击目标 |
| `HitResult` | `FHitResult` | 命中点、法线、骨骼名与组件 |
| `ImpactDirection` | `FVector` | 冲击作用方向 |
| `TraceDirection` | `FVector` | 弹道扫描方向（`TraceEnd - TraceStart` 单位化） |
| `PrimaryChannel` | `FGameplayTag` | 主伤害通道，用于注册表第一层路由 |
| `FeedbackTags` | `FGameplayTagContainer` | 表现特征标签集合 |
| `Magnitude` | `float` | 表现强度，通常为伤害总值 |
| `bLethal` | `bool` | 是否致死打击，配合顿帧全局定格参数 |
| `PredictionEventId` | `uint16` | 跨端预测事件 ID；`0` 表示不参与预测去重 |

- **线程**：仅在 GameThread 组装与消费；弱指针字段不延长 Actor 生命周期。
- **生命周期**：信号本身是纯数据，可安全复制；监听者使用前需校验弱指针有效性。
- **失败语义**：无返回值；缺失字段由各监听者按自身策略降级，不会中断广播。

```cpp
FGameFeedbackSignal Signal;
Signal.Instigator = GetInstigator();
Signal.DamageCauser = this;
Signal.Target = HitActor;
Signal.Magnitude = 42.0f;
Signal.PredictionEventId = 0;
```

### 视角与致死标签

| 符号 | 类型 | 说明 |
| :--- | :--- | :--- |
| `EGameFeedbackPerspective::LocalAttacker` | 枚举值 | 本地玩家是本次打击的发起者 / 造成者 |
| `EGameFeedbackPerspective::LocalVictim` | 枚举值 | 本地玩家是本次打击的受害者 |
| `EGameFeedbackPerspective::Bystander` | 枚举值 | 本地玩家是旁观者，免除镜头震动与触觉 |
| `TAG_Feedback_State_Lethal` | 原生标签 | `"Feedback.State.Lethal"`，伤害造成击杀时注入 |

## 原子动作参数

### 打击顿帧 `FGameFeedbackHitStopAction`

| 字段 | 类型 | 默认 | 说明 |
| :--- | :--- | :---: | :--- |
| `bEnable` | `bool` | `true` | 是否启用顿帧 |
| `VictimDuration` | `float` | `0.08f` | 受击者定格时长（秒） |
| `AttackerDuration` | `float` | `0.04f` | 攻击者定格时长（秒） |
| `TimeDilation` | `float` | `0.001f` | 定格期间时间膨胀率；编辑器钳制 `[0.0001, 1.0]`，运行时再钳制 `[0.0001, 20]` |
| `CooldownInterval` | `float` | `0.05f` | 同攻击者对同目标的触发冷却（秒） |
| `MaxCumulativeDuration` | `float` | `0.20f` | 滑动窗口内最大累积定格（秒） |
| `ResetWindow` | `float` | `0.50f` | 累积预算清零重置窗口（秒） |
| `bGlobalFreezeOnLethal` | `bool` | `false` | 致死打击是否触发全局定格特写 |
| `LethalGlobalFreezeDuration` | `float` | `0.05f` | 致死全局定格时长（秒） |
| `LethalGlobalFreezeDilation` | `float` | `0.1f` | 致死全局定格时间缩放 |
| `LethalGlobalFreezeBlendOut` | `float` | `0.02f` | 致死全局定格平滑淡出（秒） |

### 镜头震动 `FGameFeedbackCameraShakeAction`

| 字段 | 类型 | 默认 | 说明 |
| :--- | :--- | :---: | :--- |
| `AttackerShakeClass` | `TSubclassOf<UCameraShakeBase>` | 空 | 攻击者震动类 |
| `AttackerShakeScale` | `float` | `1.0f` | 攻击者震动倍率 |
| `VictimShakeClass` | `TSubclassOf<UCameraShakeBase>` | 空 | 受击者震动类 |
| `VictimShakeScale` | `float` | `1.5f` | 受击者震动倍率 |
| `MinMagnitude` | `float` | `5.0f` | 触发震动的最小强度门槛 |
| `bOrientShakeToImpactDirection` | `bool` | `true` | 是否按局部冲击方向旋转震动空间 |

### 材质受击闪白 `FGameFeedbackHitFlashAction`

| 字段 | 类型 | 默认 | 说明 |
| :--- | :--- | :---: | :--- |
| `bEnable` | `bool` | `true` | 是否启用材质闪白 |
| `TargetMode` | `EGameFeedbackFlashTargetMode` | `AllSkeletalMeshes` | `HitComponentOnly` / `AllSkeletalMeshes` / `TaggedComponentsOnly` |
| `ComponentFilterTag` | `FName` | `"Flashable"` | Tag 模式下的组件筛选标签 |
| `OverlayMaterial` | `TObjectPtr<UMaterialInterface>` | `nullptr` | 叠加闪白材质 |
| `Duration` | `float` | `0.10f` | 闪白持续时长（秒） |
| `MinMagnitude` | `float` | `3.0f` | 触发闪白的最小强度门槛 |

### 粒子特效 `FGameFeedbackVFXAction`

| 字段 | 类型 | 默认 | 说明 |
| :--- | :--- | :---: | :--- |
| `NiagaraSystem` | `TSoftObjectPtr<UNiagaraSystem>` | 空 | 粒子资产软引用，按需异步预热 |
| `bAttachToHitComponent` | `bool` | `false` | 是否附着到命中骨骼 / 组件 |
| `OrientationMode` | `EGameFeedbackVFXOrientation` | `AlignToNormal` | `AlignToNormal` / `AlignToImpactDirection` / `OppositeToTraceDirection` / `AlignToReflectedNormal` |
| `MinMagnitude` | `float` | `1.0f` | 触发特效的最小强度门槛 |

### 音效 `FGameFeedbackAudioAction`

| 字段 | 类型 | 默认 | 说明 |
| :--- | :--- | :---: | :--- |
| `SpatialImpactSound` | `TSoftObjectPtr<USoundBase>` | 空 | 世界空间受击音效 |
| `AttenuationSettings` | `TObjectPtr<USoundAttenuation>` | `nullptr` | 空间衰减配置 |
| `ConcurrencySettings` | `TObjectPtr<USoundConcurrency>` | `nullptr` | 并发限制配置 |
| `AttackerConfirmationSound` | `TSoftObjectPtr<USoundBase>` | 空 | 仅攻击者能听见的 2D 击中确认音 |
| `MinMagnitude` | `float` | `1.0f` | 触发音效的最小强度门槛 |

### 手柄触觉 `FGameFeedbackForceFeedbackAction`

| 字段 | 类型 | 默认 | 说明 |
| :--- | :--- | :---: | :--- |
| `AttackerHaptic` | `TObjectPtr<UHapticFeedbackEffect_Base>` | `nullptr` | 攻击者触觉反馈 |
| `VictimHaptic` | `TObjectPtr<UHapticFeedbackEffect_Base>` | `nullptr` | 受击者触觉反馈 |
| `MinMagnitude` | `float` | `5.0f` | 触发触觉的最小强度门槛 |

- **线程**：参数结构体为纯配置数据，运行期只读访问在 GameThread。
- **生命周期**：`TObjectPtr` 字段由所在 Definition 资产持有；软引用需经 Provider 预热。
- **失败语义**：`Magnitude` 低于对应动作 `MinMagnitude` 时该动作静默跳过；空软引用或空类直接不派发。

## 网络紧凑载荷 `FGameFeedbackEventNet`

```cpp
bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

[[nodiscard]] static FGameFeedbackEventNet FromSignal(
    const FGameFeedbackSignal& Signal,
    uint32 DefinitionId,
    uint32 DefinitionMappingVersion);

[[nodiscard]] static FGameFeedbackSignal ToSignal(const FGameFeedbackEventNet& NetEvent);

FORCEINLINE void SetDamage(float InDamage) noexcept;
[[nodiscard]] FORCEINLINE float GetDamage() const noexcept;
```

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| `Instigator` / `DamageCauser` / `Target` / `HitComponent` | `TObjectPtr` | 经 `UPackageMap::SerializeObject` 紧凑映射 |
| `ImpactLocation` | `FVector_NetQuantize` | 厘米级精度命中点 |
| `ImpactNormal` / `ImpactDirection` / `TraceDirection` | `FVector_NetQuantizeNormal` | 单位球面量化方向 |
| `DefinitionId` / `DefinitionMappingVersion` | `uint32` | 服务端权威资产身份与映射版本 |
| `PrimaryChannelIndex` | `uint16` | 主通道网络索引；未启用 FastReplication 时为 `MAX_uint16` 哨兵 |
| `DamageHalf` | `uint16` | IEEE 754 半精度伤害编码 |
| `StateFlags` | `uint8` | 经 `TBitMask<EGameFeedbackNetFlag>` 读写的状态位 |
| `PredictionEventId` | `uint16` | 跨端预测去重 ID |
| `BoneIndex` | `uint16` | 骨骼索引；未命中骨骼或静态网格体时为 `MAX_uint16` 哨兵 |

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `NetSerialize` | `Ar`、`Map`、`bOutSuccess` | `bool` | 逐字段步进结果按位与归并，任一失败即整体失败 |
| `FromSignal` | `Signal`、`DefinitionId`、`DefinitionMappingVersion` | 网络载荷 | 由表现信号构造紧凑报文 |
| `ToSignal` | `NetEvent` | 表现信号 | 由紧凑报文还原表现信号 |
| `SetDamage` / `GetDamage` | 伤害浮点 / 无 | `void` / `float` | 半精度编码与解码；NaN 归零并钳制 `[0, 65504]` |

- **线程**：权威端在 GameThread 构造并序列化，客户端在 GameThread 反序列化并回灌。
- **生命周期**：载荷为值类型；`DefinitionId` 依赖注册表索引，版本不一致时接收端拒绝解析。
- **失败语义**：`NetSerialize` 失败返回 `false` 并置 `bOutSuccess == false`，不覆盖前序字段错误。
- **带宽**：历史 Development 构建参考值（真实 `UPackageMapClient`）典型载荷 41 字节、完整载荷 44 字节；测试断言分别要求 `<= 64` 与 `<= 96` 字节。

```cpp
FGameFeedbackEventNet Event = FGameFeedbackEventNet::FromSignal(Signal, DefinitionId, MappingVersion);
Event.SetDamage(Signal.Magnitude);

FGameFeedbackSignal Restored = FGameFeedbackEventNet::ToSignal(Event);
```

## 常见错误与边界

- **预测 ID 为 0**：`PredictionEventId == 0` 既不登记也不匹配，预测去重完全跳过。
- **阈值门禁**：`Magnitude` 低于动作 `MinMagnitude` 时静默跳过，不会产生任何日志或回调。
- **半精度范围**：`SetDamage` 对 NaN 归零并钳制到 `[0, 65504]`，超出范围的伤害会被截断。
- **通道索引哨兵**：未启用 GameplayTag FastReplication 时 `PrimaryChannelIndex` 为 `MAX_uint16`，接收端不得当作有效索引。
- **版本不一致**：`DefinitionMappingVersion` 与本地注册表不同则拒绝解析，不会回退到标签匹配。
- **序列化错误**：任一子字段失败都会使 `NetSerialize` 返回 `false`，调用方不得使用部分写入的载荷。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [网络同步、裁剪与预测去重](../Architecture/NetworkAndCulling.md)
- [快速上手](../Guides/QuickStart.md)
