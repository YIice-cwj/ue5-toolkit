# GameCore 数学 API 参考

> **一句话**：帧率无关阻尼平滑、空间几何判定与高压缩比数值量化的公共 API。
> **头文件**：`Math/FGameMath.h` · `Math/FGameQuantization.h`
> **线程**：全部为无状态静态函数，线程安全且标注 `noexcept`
> **失败语义**：无错误码；非法输入按约定钳制、回退为默认值或返回 `false` / `0`，不抛异常

## 30 秒速览

- **临界阻尼**：`FGameMath::CriticalSpringDampFloat` / `Vector` / `AngleDegrees` 采用解析近似，任意大步长下无超调、数值稳定。
- **指数衰减**：`ExpDecayFloat` / `AngleDegrees` / `Vector` / `Quat` 以半衰期参数化，数学上严格帧率无关；四元数版本自动走最短大圆路径。
- **空间几何**：`SignedYawDeltaDegrees` 返回水平面有符号夹角 `[-180°, 180°]`；`IsInVisionCone` 先做距离平方早退，再按半张角判定。
- **量化压缩**：`FGameQuantization` 提供半精度、区间 uint8/uint16、`PackVectorToUint64`（X 21 / Y 21 / Z 22 位）与 `PackYawPitch`（8+8 位）。
- **约束**：输入有限时，`DeltaSeconds <= 0` 的平滑函数原样返回当前值；`HalfLifeSeconds` 过小时瞬时收敛；`IsInVisionCone` 在 `MaxDistance <= 0` 或半张角 `< 0` 时恒为 `false`。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FGameMath::CriticalSpringDampFloat` | 标量临界阻尼平滑 | 无状态静态函数；`DeltaSeconds <= 0` 原样返回当前状态 |
| `FGameMath::CriticalSpringDampVector` | 三维位置与速度临界阻尼平滑 | 任意大步长下无超调、无发散 |
| `FGameMath::ExpDecayFloat` | 半衰期指数衰减平滑 | `HalfLifeSeconds <= 0.00001` 视为瞬时收敛；非有限值返回目标值 |
| `FGameMath::ExpDecayQuat` | 四元数最短大圆路径平滑 | 含 `NaN` 时返回目标值 |
| `FGameMath::SignedYawDeltaDegrees` | 计算水平面有符号夹角 | 仅 XY 平面投影；投影接近零返回 `0` |
| `FGameMath::IsInVisionCone` | 视锥可见性判定 | `MaxDistance <= 0`、半张角 `< 0` 或非有限时恒为 `false` |
| `FGameQuantization::FloatToHalf` | float 转 16 位半精度 | IEEE 754 16 位半精度编码 |
| `FGameQuantization::QuantizeFloatToUint8` | 区间量化为 8 位 | 输入非有限或区间反向时返回 `0` |
| `FGameQuantization::PackVectorToUint64` | 三维坐标压缩为 64 位 | 默认分辨率 0.1 覆盖约 ±1.048 km，超界钳制不报错 |
| `FGameQuantization::PackYawPitch` | 偏航 / 俯仰压缩为 16 位 | 还原误差 ≤ 1.5°；偏航在 ±180° 存在环绕 |

## 1. 游戏数学 `FGameMath`

```cpp
class GAMECORE_API FGameMath {
public:
    struct FSpringFloatState {
        float Value = 0.0f;
        float Velocity = 0.0f;

        bool operator==(const FSpringFloatState&) const = default;
    };

    struct FSpringVectorState {
        FVector Value = FVector::ZeroVector;
        FVector Velocity = FVector::ZeroVector;

        bool operator==(const FSpringVectorState&) const = default;
    };

    static constexpr float MIN_SMOOTH_TIME = 0.0001f;

    [[nodiscard]] static FSpringFloatState CriticalSpringDampFloat(
        FSpringFloatState CurrentState,
        float TargetValue,
        float SmoothTime,
        float DeltaSeconds
    ) noexcept;

    [[nodiscard]] static FSpringVectorState CriticalSpringDampVector(
        const FSpringVectorState& CurrentState,
        const FVector& TargetValue,
        float SmoothTime,
        float DeltaSeconds
    ) noexcept;

    [[nodiscard]] static FSpringFloatState CriticalSpringDampAngleDegrees(
        FSpringFloatState CurrentState,
        float TargetAngleDegrees,
        float SmoothTime,
        float DeltaSeconds
    ) noexcept;

    [[nodiscard]] static float SignedYawDeltaDegrees(
        const FVector& ForwardDir,
        const FVector& ToTargetDir
    ) noexcept;

    [[nodiscard]] static bool IsInVisionCone(
        const FVector& OffsetToTarget,
        const FVector& ConeForward,
        float MaxDistance,
        float HalfAngleDegrees
    ) noexcept;

    [[nodiscard]] static float ExpDecayFloat(
        float CurrentValue,
        float TargetValue,
        float HalfLifeSeconds,
        float DeltaSeconds
    ) noexcept;

    [[nodiscard]] static float ExpDecayAngleDegrees(
        float CurrentDegrees,
        float TargetDegrees,
        float HalfLifeSeconds,
        float DeltaSeconds
    ) noexcept;

    [[nodiscard]] static FVector ExpDecayVector(
        const FVector& CurrentValue,
        const FVector& TargetValue,
        float HalfLifeSeconds,
        float DeltaSeconds
    ) noexcept;

    [[nodiscard]] static FQuat ExpDecayQuat(
        const FQuat& CurrentValue,
        const FQuat& TargetValue,
        float HalfLifeSeconds,
        float DeltaSeconds
    ) noexcept;
};
```

### 1.1 临界阻尼弹簧

参数约定：`SmoothTime` 为近似收敛平滑时长（秒，内部下限 `MIN_SMOOTH_TIME = 0.0001f`）；`DeltaSeconds <= 0` 时原样返回当前状态。

| 函数 | 返回 | 说明 |
| :--- | :--- | :--- |
| `CriticalSpringDampFloat` | `FSpringFloatState` | 标量位置与速度的下一帧状态 |
| `CriticalSpringDampVector` | `FSpringVectorState` | 三维位置与速度的下一帧状态 |
| `CriticalSpringDampAngleDegrees` | `FSpringFloatState` | 角度（度）沿 `[-180, 180]` 最短圆弧收敛，输出角度标准化 |

- **模型**：`ω = 2 / SmoothTime`，`ExpFactor = 1 / (1 + x + 0.48x² + 0.235x³)`，其中 `x = ω · Δt`。
- **行为**：任意大时间步长下无数值发散与超调；携带初速度负脉冲时按解析解自然响应。
- **线程**：纯静态函数，无共享状态。

### 1.2 半衰期指数衰减

| 函数 | 返回 | 说明 |
| :--- | :--- | :--- |
| `ExpDecayFloat` | `float` | 标量平滑值 |
| `ExpDecayAngleDegrees` | `float` | 角度平滑值，标准化至 `[-180, 180]` |
| `ExpDecayVector` | `FVector` | 三维向量平滑值 |
| `ExpDecayQuat` | `FQuat` | 归一化四元数平滑值，自适应最短路径 Slerp |

- **模型**：`λ = ln2 / HalfLifeSeconds`，`DecayFactor = 1 - e^(-λ · Δt)`，相同总时长下与子步切分方式无关。
- **边界**：`DeltaSeconds <= 0` 返回当前值；`HalfLifeSeconds <= 0.00001` 视为瞬时收敛；标量与向量版本在当前值或目标值非有限时返回目标值，四元数版本在含 `NaN` 时返回目标值。
- **四元数**：通过内积符号检测选择最短大圆路径，避免 360° 倒转跳变。

### 1.3 空间几何

| 函数 | 参数 | 返回 |
| :--- | :--- | :--- |
| `SignedYawDeltaDegrees` | 自身前向向量、指向目标的方向或位移向量 | 水平面有符号夹角 `[-180°, 180°]`；任一向量水平投影接近零返回 `0` |
| `IsInVisionCone` | 视点至目标的位移、视锥中心轴、最大距离、半张角（度） | 目标在距离与夹角范围内返回 `true` |

- **`IsInVisionCone` 边界**：`MaxDistance <= 0`、半张角 `< 0`、`MaxDistance` / 半张角非有限或 `ConeForward` 长度接近零时返回 `false`；半张角 `>= 180` 时短路为全向球体并返回 `true`；位移接近零视为在锥内。
- **性能**：先做距离平方比较早退，再用 `FMath::InvSqrt` 计算夹角余弦，避免多余开方与反三角函数。
- **`SignedYawDeltaDegrees`**：仅在 XY 平面投影计算，忽略 Z 轴。

### 最小示例

```cpp
// 1. 三维摄像机平滑追踪（临界阻尼弹簧）
FGameMath::FSpringVectorState CameraSpringState{CurrentCamPos, CurrentCamVel};
void Tick(float DeltaSeconds) {
    CameraSpringState = FGameMath::CriticalSpringDampVector(
        CameraSpringState,
        TargetActorLocation,
        /* SmoothTime = */ 0.2f,
        DeltaSeconds);
    SetActorLocation(CameraSpringState.Value);
}

// 2. 帧率无关四元数平滑旋转（半衰期 0.1 秒）
CurrentRot = FGameMath::ExpDecayQuat(
    CurrentRot,
    DesiredFacingQuat,
    /* HalfLifeSeconds = */ 0.1f,
    DeltaSeconds);
SetActorRotation(CurrentRot);

// 3. AI 视锥感知快速检测
const FVector EyeToTarget = TargetActor->GetActorLocation() - AIPawn->GetActorLocation();
const bool bSeen = FGameMath::IsInVisionCone(
    EyeToTarget,
    AIPawn->GetActorForwardVector(),
    /* MaxDistance = */ 1500.0f,
    /* HalfAngleDegrees = */ 45.0f);
```

## 2. 数值量化 `FGameQuantization`

```cpp
class GAMECORE_API FGameQuantization {
public:
    FGameQuantization() = delete;

    [[nodiscard]] static uint16 FloatToHalf(float InValue) noexcept;
    [[nodiscard]] static float HalfToFloat(uint16 InHalf) noexcept;

    [[nodiscard]] static uint8 QuantizeFloatToUint8(
        float InValue, float InMinValue, float InMaxValue) noexcept;
    [[nodiscard]] static float DequantizeUint8ToFloat(
        uint8 InQuantized, float InMinValue, float InMaxValue) noexcept;

    [[nodiscard]] static uint16 QuantizeFloatToUint16(
        float InValue, float InMinValue, float InMaxValue) noexcept;
    [[nodiscard]] static float DequantizeUint16ToFloat(
        uint16 InQuantized, float InMinValue, float InMaxValue) noexcept;

    [[nodiscard]] static uint64 PackVectorToUint64(
        const FVector& InVector, double InResolution = 0.1) noexcept;
    [[nodiscard]] static FVector UnpackUint64ToVector(
        uint64 InPacked, double InResolution = 0.1) noexcept;

    [[nodiscard]] static uint16 PackYawPitch(
        float InYawDegrees, float InPitchDegrees) noexcept;
    static void UnpackYawPitch(
        uint16 InPacked, float& OutYawDegrees, float& OutPitchDegrees) noexcept;
};
```

| 压缩原语 | 原始尺寸 | 压缩后 | 压缩比 | 精度与边界 |
| :--- | :--- | :--- | :--- | :--- |
| `FloatToHalf` / `HalfToFloat` | 4 字节（32-bit） | 2 字节（16-bit） | 50% | IEEE 754 16 位半精度编码 |
| `QuantizeFloatToUint8` | 4 字节（32-bit） | 1 字节（8-bit） | 25% | 自定义区间 `[Min, Max]` 256 等分 |
| `QuantizeFloatToUint16` | 4 字节（32-bit） | 2 字节（16-bit） | 50% | 自定义区间 `[Min, Max]` 65536 等分 |
| `PackVectorToUint64` | 24 字节（3 × double） | 8 字节（64-bit） | 33.3% | X/Y 各 21 位、Z 22 位有符号离散打包 |
| `PackYawPitch` | 8 字节（2 × float） | 2 字节（16-bit） | 25% | 偏航 8 位 + 俯仰 8 位，还原误差 ≤ 1.5° |

### 2.1 区间量化

- **参数**：`InValue` 为输入值，`[InMinValue, InMaxValue]` 为闭区间；量化结果分别落在 `[0, 255]` 或 `[0, 65535]`。
- **边界**：任一输入非有限或 `InMaxValue <= InMinValue` 时 `Quantize*` 返回 `0`；`Dequantize*` 在区间非法时返回 `InMinValue`（其非有限则返回 `0`）。
- **用途**：网络同步、回放录制与内存敏感场景的有损压缩。

### 2.2 64 位三维向量打包

- **位域**：X 为 21 位有符号整数 `[-1048576, 1048575]`；Y 为 21 位 `[-1048576, 1048575]`；Z 为 22 位 `[-2097152, 2097151]`。
- **防溢出**：编码前在双精度浮点域钳制，避免有符号整数溢出 UB；解包用算术右移恢复符号位。
- **默认范围**：`InResolution = 0.1`（UE 默认单位下即 1mm）时，X/Y 覆盖约 `[-1.048 km, +1.048 km]`。
- **非法输入**：`InResolution` 非有限或 `<= 0` 时回退为 `0.1`；坐标轴非有限时该轴按 `0` 编码。
- **精度**：解包误差不超过一个 `InResolution` 单位。

### 2.3 偏航 / 俯仰打包

- **编码**：偏航角先规范到 `[-180°, 180°)` 并量化为 8 位；俯仰角钳制到 `[-90°, 90°]` 后量化为 8 位，二者拼成 16 位整数。
- **边界**：非有限输入按 `0` 处理；偏航在 ±180° 处存在固有环绕，俯仰超出范围被钳制。
- **精度**：还原误差 ≤ 1.5°。

### 最小示例

```cpp
// 1. 三维空间坐标网络压缩（24 字节 -> 8 字节）
const FVector WorldPos(1500.5, 3200.2, 500.8);
const uint64 PackedVector =
    FGameQuantization::PackVectorToUint64(WorldPos, /* Resolution = */ 0.1);
const FVector RestoredPos =
    FGameQuantization::UnpackUint64ToVector(PackedVector, /* Resolution = */ 0.1);

// 2. 玩家瞄准欧拉角压缩（8 字节 -> 2 字节）
const uint16 PackedAim = FGameQuantization::PackYawPitch(CharacterYaw, CharacterPitch);

float AimYaw = 0.0f;
float AimPitch = 0.0f;
FGameQuantization::UnpackYawPitch(PackedAim, AimYaw, AimPitch);
```

## 常见错误与边界

- **忽略非正 DeltaSeconds**：输入有限时，平滑函数在 `DeltaSeconds <= 0` 时原样返回当前值，不会推进或收敛。
- **把 SmoothTime 当精确收敛时间**：它是近似平滑时长，实际为指数逼近；内部下限钳制为 `0.0001s`。
- **视锥参数非法**：`MaxDistance <= 0` 或半张角 `< 0` 时 `IsInVisionCone` 恒为 `false`，不是异常或断言。
- **量化区间反向**：`InMinValue >= InMaxValue` 时量化静默返回 `0`，调用方需自行保证区间有效。
- **超出向量打包范围**：坐标超过约 ±1.048km（默认分辨率）会被钳制到边界，不报错，解包后无法还原。
- **偏航角环绕**：`PackYawPitch` 将偏航规范到 `[-180°, 180°)`，跨 ±180° 的目标会出现方向翻转，需在业务层做最短路径处理。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [快速上手](../Guides/QuickStart.md)
- [性能基准](../Architecture/Performance.md)
- [确定性 API](Determinism.md)
