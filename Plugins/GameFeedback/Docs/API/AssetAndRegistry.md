# GameFeedback 表现资产与注册表 API 参考

> **一句话**：表现配置资产、四级匹配注册表与软引用去重预热服务的公共 API。
> **头文件**：`Asset/GameFeedbackDefinition.h`、`Asset/GameFeedbackRegistry.h`、`System/GameFeedbackDefinitionProvider.h` · **线程**：仅 GameThread；异步加载的缓存写入与结果返回在 GameThread · **失败语义**：服务解析失败返回 `nullptr`，ID 查询失败返回 `0`，软引用查询返回空软指针

## 30 秒速览

- **资产**：`UGameFeedbackDefinition` 聚合 HitStop、CameraShake、HitFlash、VFX、Audio、Haptics 六组原子动作。
- **匹配**：`UGameFeedbackRegistry` 按“精确通道 → 标签深度 → 复合规则优先级 → 默认兜底”四级解析，未命中时回退兜底资产。
- **身份**：`MakeDefinitionId` 由完整软对象路径 CRC32 生成稳定 ID；`DefinitionMappingVersion` 由注册表内容归一化哈希生成。
- **服务**：`UGameFeedbackDefinitionProvider` 统一解析，并按 `FSoftObjectPath` 去重预热 Definition、VFX 与 Audio。
- **约束**：只异步预热，不阻塞主线程同步加载；未驻留资产在解析时返回 `nullptr`。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UGameFeedbackRegistry::ResolveDefinitionPtr` | 四级流水线解析最优表现资产 | 仅 GameThread；均未命中返回兜底资产（可能为空） |
| `UGameFeedbackRegistry::RebuildDefinitionIndex` | 重建标签索引、ID 索引与映射版本 | 运行时修改注册表后须显式调用 |
| `UGameFeedbackRegistry::GetDefinitionId` | 指针直查稳定 ID | 未登记或 CRC32 冲突返回 `0` |
| `UGameFeedbackRegistry::FindDefinitionById` | 按 ID 反查资产软引用 | 未知或冲突 ID 返回空软引用 |
| `UGameFeedbackRegistry::MakeDefinitionId` | 由完整软对象路径生成稳定 ID | 空路径返回 `0`；哈希为 `0` 时映射为 `1` |
| `UGameFeedbackDefinitionProvider::Resolve` | 由信号解析定义指针 | 未配置注册表或资产未驻留返回 `nullptr` |
| `UGameFeedbackDefinitionProvider::ResolveById` | 按 ID + 版本还原定义 | 未知、冲突、版本不一致或未加载返回 `nullptr` |
| `UGameFeedbackDefinitionProvider::PreloadSoftAsset` | 按路径去重异步预热 VFX / Audio | 不阻塞主线程；加载后随世界保活 |
| `UGameFeedbackDefinitionProvider::GetPtr` | 获取世界级服务指针 | 仅 GameThread；未就绪返回 `nullptr` |

## 资产定义 `UGameFeedbackDefinition`

```cpp
UCLASS(BlueprintType)
class GAMEFEEDBACK_API UGameFeedbackDefinition : public UPrimaryDataAsset {
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actions")
    FGameFeedbackHitStopAction HitStop;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actions")
    FGameFeedbackCameraShakeAction CameraShake;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actions")
    FGameFeedbackHitFlashAction HitFlash;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actions")
    FGameFeedbackVFXAction VFX;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actions")
    FGameFeedbackAudioAction Audio;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actions")
    FGameFeedbackForceFeedbackAction Haptics;
};
```

| 成员 | 类型 | 说明 |
| :--- | :--- | :--- |
| `HitStop` | `FGameFeedbackHitStopAction` | 攻击者 / 受击者顿帧与致死全局定格参数 |
| `CameraShake` | `FGameFeedbackCameraShakeAction` | 攻击者 / 受击者镜头震动类与倍率 |
| `HitFlash` | `FGameFeedbackHitFlashAction` | Overlay 材质、目标模式与持续时长 |
| `VFX` | `FGameFeedbackVFXAction` | Niagara 软引用、附着策略与朝向模式 |
| `Audio` | `FGameFeedbackAudioAction` | 空间受击音与攻击者 2D 确认音 |
| `Haptics` | `FGameFeedbackForceFeedbackAction` | 攻击者 / 受击者手柄触觉反馈 |

## 注册表 `UGameFeedbackRegistry`

### 四级匹配流水线

| 层级 | 数据源 | 规则 | 未命中行为 |
| :---: | :--- | :--- | :--- |
| 1 | `ExactChannelMap` | 按 `PrimaryChannel` 精确查找 | 进入第 2 层 |
| 2 | `ExactFeedbackTagMap` | `RebuildDefinitionIndex` 预排序为深度降序、同深度 `FName` 字典序升序；首个 `FeedbackTags.HasTag` 命中即返回 | 进入第 3 层 |
| 3 | `CompositeRules` | 按 `Priority` 降序，首个 `FGameplayTagQuery` 命中即返回 | 进入第 4 层 |
| 4 | `DefaultFallbackDefinition` | 直接返回兜底资产 | 返回空软引用 |

标签索引在 `PostLoad` / `RebuildDefinitionIndex` 时预排序，运行期 `ResolveDefinitionPtr` 只做数组遍历，避免高频受击时重复计算标签树深度。

### 公共接口

```cpp
[[nodiscard]] TSoftObjectPtr<UGameFeedbackDefinition> ResolveDefinitionPtr(
    const FGameplayTag& PrimaryChannel,
    const FGameplayTagContainer& FeedbackTags) const;

void RebuildDefinitionIndex();

[[nodiscard]] uint32 GetDefinitionId(const UGameFeedbackDefinition* Definition) const;
[[nodiscard]] TSoftObjectPtr<UGameFeedbackDefinition> FindDefinitionById(uint32 DefinitionId) const;
[[nodiscard]] uint32 GetDefinitionMappingVersion() const;
[[nodiscard]] static uint32 MakeDefinitionId(const FSoftObjectPath& DefinitionPath);
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `ResolveDefinitionPtr` | `PrimaryChannel`、`FeedbackTags` | 最优资产软引用 | 四级流水线入口；均未命中时返回兜底资产 |
| `RebuildDefinitionIndex` | 无 | `void` | 重建标签有序索引、ID 索引、冲突集合与映射版本 |
| `GetDefinitionId` | `Definition` | `uint32` | 已登记且无冲突返回非零 ID；未登记或冲突返回 `0` |
| `FindDefinitionById` | `DefinitionId` | 软引用 | 未知或冲突 ID 返回空软引用 |
| `GetDefinitionMappingVersion` | 无 | `uint32` | 空映射返回 `0` |
| `MakeDefinitionId` | `DefinitionPath` | `uint32` | 空路径返回 `0`；哈希结果为 `0` 时映射为 `1` |

- **线程**：索引查询为只读纯逻辑，仅在 GameThread 调用。
- **生命周期**：`PostLoad` 自动构建索引；编辑器属性变更经 `PostEditChangeProperty` 重建。
- **失败语义**：`ResolveDefinitionPtr` 永不返回空指针以外的错误码；软引用是否可立即访问由调用方检查。

### 稳定身份与版本

- **确定性 CRC32 ID**：`MakeDefinitionId` 对完整软对象路径生成 ID，`RebuildDefinitionIndex` 同步维护 `DefinitionIdByPath` 与 `DefinitionsById`，实现 O(1) 直查。
- **冲突拉黑**：不同路径碰撞为同一 CRC32 时加入 `ConflictedDefinitionIds`，该 ID 禁止网络传输与远端解析。
- **映射版本**：默认资产、通道表、标签表与复合规则（`FGameplayTagQuery` 的 `TokenStream` 与 `TagDictionary`，复合规则经 `Algo::StableSort`）归一化哈希生成 `DefinitionMappingVersion`；客户端与服务端不一致时拒绝解析。

## 解析与预热服务 `UGameFeedbackDefinitionProvider`

`UGameFeedbackDefinitionProvider` 是 `UWorldSubsystem`，在包含 Dedicated Server 的游戏世界创建，是运行期访问表现资产与媒体软引用的唯一托管来源。

```cpp
const UGameFeedbackDefinition* Resolve(const FGameFeedbackSignal& Signal);

[[nodiscard]] uint32 GetDefinitionId(const UGameFeedbackDefinition* Definition) const;
[[nodiscard]] uint32 GetDefinitionMappingVersion() const;
const UGameFeedbackDefinition* ResolveById(uint32 DefinitionId, uint32 DefinitionMappingVersion);

void PreloadFeedbackBundle(TConstArrayView<TSoftObjectPtr<UGameFeedbackDefinition>> Definitions);
void PreloadSoftAsset(const FSoftObjectPath& AssetPath);

[[nodiscard]] int32 GetActivePreloadHandleCount() const;
[[nodiscard]] static UGameFeedbackDefinitionProvider* GetPtr(const UObject* WorldContextObject);
```

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `Resolve` | `Signal` | 定义指针 | 未配置注册表返回 `nullptr`，并输出一次性 Warning |
| `GetDefinitionId` | `Definition` | `uint32` | 带版本校验的指针直查缓存；未登记、冲突或未配置返回 `0` |
| `GetDefinitionMappingVersion` | 无 | `uint32` | 未配置注册表返回 `0` |
| `ResolveById` | `DefinitionId`、`DefinitionMappingVersion` | 定义指针 | 未知、冲突、版本不一致或未加载返回 `nullptr` |
| `PreloadFeedbackBundle` | 定义软指针集合 | `void` | 批量异步预热 |
| `PreloadSoftAsset` | `AssetPath` | `void` | 按路径去重预热 VFX / Audio，加载后随世界保活 |
| `GetActivePreloadHandleCount` | 无 | `int32` | 在飞句柄数，供观测与测试 |
| `GetPtr` | `WorldContextObject` | 服务指针 | 未就绪返回 `nullptr` |

- **线程**：全部接口仅限 GameThread；异步加载完成后的缓存写入也在 GameThread。
- **生命周期**：已解析定义写入 `LoadedDefinitions`，媒体软资产写入 `PreloadedMediaAssets`；`Deinitialize` 遍历 `DefinitionLoads` 与 `PendingMediaLoads` 调用 `CancelHandle()` 并清空缓存。
- **失败语义**：`RequestAsyncLoad` 若同步完成则立即入缓存返回，不残留已完成句柄；加载中或失败返回 `nullptr`。

```cpp
UGameFeedbackDefinitionProvider* Provider = UGameFeedbackDefinitionProvider::GetPtr(this);
if (Provider != nullptr) {
    Provider->PreloadSoftAsset(VfxPath);
    if (const UGameFeedbackDefinition* Definition = Provider->Resolve(Signal)) {
        PlayLocalFeedback(*Definition);
    }
}
```

## 常见错误与边界

- **注册表未配置**：`Resolve` 返回 `nullptr` 并只告警一次；调用方需容忍无表现资产。
- **资产未驻留**：解析不会同步加载，返回 `nullptr`；应先经 `PreloadFeedbackBundle` / `PreloadSoftAsset` 预热。
- **版本不一致**：`ResolveById` 直接返回 `nullptr`，不会退回本地注册表重新匹配。
- **CRC32 冲突**：冲突 ID 的 `GetDefinitionId` 返回 `0`，该资产不会被网络派发。
- **运行时修改注册表**：运行时变更后需显式调用 `RebuildDefinitionIndex`；编辑器内由 `PostEditChangeProperty` 自动重建。
- **子系统销毁**：`Deinitialize` 后所有缓存与在飞句柄清空，不应继续持有 `Resolve` 返回的裸指针。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [网络同步、裁剪与预测去重](../Architecture/NetworkAndCulling.md)
- [快速上手](../Guides/QuickStart.md)
