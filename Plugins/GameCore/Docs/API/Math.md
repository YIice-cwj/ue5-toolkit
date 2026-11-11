# GameCore 游戏数学与数值量化 API 参考

本文档汇总 `GameCore` 提供的帧率无关平滑阻尼、空间几何计算与高压缩比数值量化打包原语（全部位于 `Math/` 目录）：
- `FGameMath`：帧率无关解析临界阻尼弹簧、半衰期指数衰减平滑与视锥/偏航几何判定
- `FGameQuantization`：半精度浮点、自定义范围数值量化、64 位三维向量打包与 16 位视线角压缩

---

## 1. 帧率无关游戏数学库 `FGameMath`

头文件：`#include "Math/FGameMath.h"`

### 核心特性与数学模型
- **解析临界阻尼弹簧（Critical Spring Damper）**：
  - 传统显式欧拉弹簧在帧率波动或掉帧（如单帧 100ms）时容易发生过冲、震荡乃至数值发散（Explosion）；
  - `CriticalSpringDampFloat` 与 `CriticalSpringDampVector` 采用 4 阶 Padé 解析逼近：
    $$\text{ExpFactor} = \frac{1}{1 + x + 0.48x^2 + 0.235x^3}, \quad x = \omega \cdot \Delta t, \quad \omega = \frac{2}{\text{SmoothTime}}$$
  - **在任意极大时间步长下绝对数值稳定**，无任何震荡与超调，以最快速度平滑收敛至目标值，且携带初速度负脉冲时物理响应自然；
- **半衰期参数化指数衰减（Half-Life Exponential Decay）**：
  - 传统 `FMath::FInterpTo(Current, Target, DeltaTime, InterpSpeed)` 的收敛速度受帧率影响严重（30 FPS 与 240 FPS 轨迹差异明显）；
  - `ExpDecayFloat` / `ExpDecayVector` / `ExpDecayQuat` 以半衰期时长 $t_{1/2}$（秒）参数化：
    $$\lambda = \frac{\ln 2}{t_{1/2}}, \quad \text{DecayFactor} = 1 - e^{-\lambda \Delta t}$$
  - **数学上严格保证帧率无关**：无论被切分为多少个微小子步，相同物理总时长下的收敛结果绝对恒等；
  - 四元数版本（`ExpDecayQuat`）通过内积点积符号检测，自适应选择最短大圆路径球面插值（Slerp），杜绝 360 度倒转跳变；
- **空间几何极速计算**：
  - `SignedYawDeltaDegrees(ForwardDir, ToTargetDir)`：在水平面（XY）投影上计算从前向到目标朝向的有符号夹角 $[-180^\circ, 180^\circ]$，完美适用于受击八向动画判定与怪物转向混合；
  - `IsInVisionCone(OffsetToTarget, ConeForward, MaxDistance, HalfAngleDegrees)`：
    - 集成距离平方早退（`DistSq > MaxDistSq`）拦截；
    - 180° 全向短路优化；
    - 结合 `FMath::InvSqrt` 快速逆平方根计算，全程避免任何昂贵的多余开方与三角函数反解。

```cpp
// 1. 三维摄像机平滑追踪（临界阻尼弹簧）
FGameMath::FSpringVectorState CameraSpringState{CurrentCamPos, CurrentCamVel};
void Tick(float DeltaSeconds) {
    CameraSpringState = FGameMath::CriticalSpringDampVector(
        CameraSpringState,
        TargetActorLocation,
        /* SmoothTime = */ 0.2f,
        DeltaSeconds
    );
    SetActorLocation(CameraSpringState.Value);
}

// 2. 帧率无关四元数平滑旋转（半衰期 0.1 秒）
FQuat CurrentRot = GetActorQuat();
FQuat TargetRot = DesiredFacingQuat;
CurrentRot = FGameMath::ExpDecayQuat(CurrentRot, TargetRot, /* HalfLifeSeconds = */ 0.1f, DeltaSeconds);
SetActorRotation(CurrentRot);

// 3. AI 视锥感知快速检测
const FVector EyeToTarget = TargetActor->GetActorLocation() - AIPawn->GetActorLocation();
const bool bSeen = FGameMath::IsInVisionCone(
    EyeToTarget,
    AIPawn->GetActorForwardVector(),
    /* MaxDistance = */ 1500.0f,
    /* HalfAngleDegrees = */ 45.0f
);
```

---

## 2. 高压缩比数值量化打包 `FGameQuantization`

头文件：`#include "Math/FGameQuantization.h"`

### 适用场景与带宽优化
在联机网络同步（RPC / Snapshot / Actor Replication）与物理回放录制中，原始浮点与三维坐标占用大量带宽与内存。`FGameQuantization` 提供高精度、防溢出的确定性无堆分配数据打包设施：

| 压缩原语 | 原始尺寸 | 压缩后尺寸 | 压缩比 | 精度说明与边界保护 |
| :--- | :--- | :--- | :--- | :--- |
| `FloatToHalf` / `HalfToFloat` | 4 字节 (32-bit) | 2 字节 (16-bit) | 50% | IEEE 754 16位半精度编码 |
| `QuantizeFloatToUint8` | 4 字节 (32-bit) | 1 字节 (8-bit) | 25% | 自定义区间 `[Min, Max]` 256 等分均匀量化 |
| `QuantizeFloatToUint16` | 4 字节 (32-bit) | 2 字节 (16-bit) | 50% | 自定义区间 `[Min, Max]` 65536 等分均匀量化 |
| `PackVectorToUint64` | 24 字节 (3x double) | 8 字节 (64-bit) | **33.3%** | X(21-bit), Y(21-bit), Z(22-bit) 离散打包 |
| `PackYawPitch` | 8 字节 (2x float) | 2 字节 (16-bit) | **25%** | 偏航角(8-bit) + 俯仰角(8-bit)，还原误差 $\le 1.5^\circ$ |

### 64 位三维向量打包技术细节
- **位域排布**：
  - X 轴：21 位有符号整数，范围 $[-1048576, 1048575]$；
  - Y 轴：21 位有符号整数，范围 $[-1048576, 1048575]$；
  - Z 轴：22 位有符号整数，范围 $[-2097152, 2097151]$；
- **防溢出与解包符号扩展**：
  - 编码前在双精度浮点域内严格执行下上限钳制，绝不发生有符号整数溢出 UB；
  - 解包时采用 C++ 算术右移（`SignExtendedX = static_cast<int32>(RawX << 11) >> 11`）执行符号位自动恢复扩展，耗时仅 1~2 条机器指令；
  - 以默认分辨率 `0.1`（即 10cm）计算，可完整覆盖 $[-104.8\text{km}, +104.8\text{km}]$ 的超大规模开放世界地图坐标。

```cpp
// 1. 三维空间坐标网络压缩（从 24 字节压缩到 8 字节）
const FVector WorldPos(1500.5, 3200.2, 500.8);
const uint64 PackedVector = FGameQuantization::PackVectorToUint64(WorldPos, /* Resolution = */ 0.1);

// 网络解包还原（误差小于 0.1cm）
const FVector RestoredPos = FGameQuantization::UnpackUint64ToVector(PackedVector, /* Resolution = */ 0.1);

// 2. 玩家瞄准欧拉角压缩（从 8 字节压缩到 2 字节）
const uint16 PackedAim = FGameQuantization::PackYawPitch(CharacterYaw, CharacterPitch);

// 解包
float AimYaw = 0.0f;
float AimPitch = 0.0f;
FGameQuantization::UnpackYawPitch(PackedAim, AimYaw, AimPitch);
```
