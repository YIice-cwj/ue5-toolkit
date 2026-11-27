# GameFeedback 表现配置资产与注册表 API 参考

本文档详细介绍打击表现资产（`UGameFeedbackDefinition`）、表现路由注册表（`UGameFeedbackRegistry`）以及运行时资产解析服务（`UGameFeedbackDefinitionProvider`）。

- 头文件：
  - `#include "Asset/GameFeedbackDefinition.h"`
  - `#include "Asset/GameFeedbackRegistry.h"`
  - `#include "System/GameFeedbackDefinitionProvider.h"`

---

## 1. 单条表现资产定义 (`UGameFeedbackDefinition`)

`UGameFeedbackDefinition` 继承自 `UPrimaryDataAsset`，聚合了针对某种打击风格的完整视听配置：

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

---

## 2. 全局表现映射注册表 (`UGameFeedbackRegistry`)

`UGameFeedbackRegistry` 负责根据信号中的伤害通道与特征标签，以最优性能匹配出最具体的表现资产：

### 2.1 四级匹配流水线 (Resolution Hierarchy)

```mermaid
flowchart TD
    In[输入 Signal: PrimaryChannel & FeedbackTags] --> Tier1{1. ExactChannelMap 命中?}
    Tier1 -- 是且非空 --> Ret1[返回对应 Definition]
    Tier1 -- 否 --> Tier2{2. ExactFeedbackTagMap 命中?}
    
    Tier2 -- 是 --> DepthCheck[按标签树深度降序排序<br>同深度按 FName 字典序升序早出]
    DepthCheck --> Ret2[返回深度最具体 Definition]
    Tier2 -- 否 --> Tier3{3. CompositeRules 复合查询命中?}

    Tier3 -- 是 --> Ret3[按 Priority 降序返回首个匹配资产]
    Tier3 -- 否 --> Fallback[4. 返回 DefaultFallbackDefinition]
```

1. **第一层：精确主伤害通道 (`ExactChannelMap`)**
   - 字典快速查找 `PrimaryChannel`（例如 `Damage.Channel.Fire` 直接映射到火焰打击资产）。
2. **第二层：特征标签深度匹配 (`ExactFeedbackTagMap`)**
   - 在 `PostLoad` 或 `RebuildDefinitionIndex` 时，字典条目预排序构建为 `SortedExactFeedbackTags`（深度深者优先，同深度按键字典序升序）；
   - 运行时 `ResolveDefinitionPtr` 直接遍历预排序数组，首个匹配 `FeedbackTags.HasTag` 的项即为最优项直接返回，避免高频受击时反复计算标签树深度。
3. **第三层：复合规则查询 (`CompositeRules`)**
   - 基于 `FGameplayTagQuery` 的高级组合匹配（如“受击者是机械 AND 武器是重锤”）；
   - 数组按 `Priority` 降序排列，首个匹配项胜出。
4. **第四层：缺省兜底资产 (`DefaultFallbackDefinition`)**
   - 上述三层均未命中时返回通用兜底打击资产，保证系统永不落空。

### 2.2 确定性网络身份与版本管理

```cpp
class GAMEFEEDBACK_API UGameFeedbackRegistry : public UDataAsset {
public:
    void RebuildDefinitionIndex();
    uint32 GetDefinitionId(const UGameFeedbackDefinition* Definition) const;
    TSoftObjectPtr<UGameFeedbackDefinition> FindDefinitionById(uint32 DefinitionId) const;
    uint32 GetDefinitionMappingVersion() const;
    static uint32 MakeDefinitionId(const FSoftObjectPath& DefinitionPath);
};
```

- **稳定 CRC32 ID 与 O(1) 索引**：通过 `MakeDefinitionId` 为完整软对象路径生成确定性 ID（0 映射为 1），并在 `RebuildDefinitionIndex` 中同步缓存 `DefinitionIdByPath` 实现 O(1) 路径直查；编辑器修改属性时经 `PostEditChangeProperty` 自动重建索引；
- **冲突检测**：`RebuildDefinitionIndex` 会自动检测不同路径的 CRC32 冲突，冲突资产加入黑名单，禁止网络传输；
- **TokenStream 映射版本**：将默认资产、通道表、标签表与复合规则（基于 `FGameplayTagQuery` 真实表达式 `TokenStream` 与 `TagDictionary`，且规则采用 `Algo::StableSort` 稳定排序）归一化哈希生成 `DefinitionMappingVersion`，客户端与服务端版本不一致时直接拒绝解析。

---

## 3. 资产解析与统一软引用预热服务 (`UGameFeedbackDefinitionProvider`)

`UGameFeedbackDefinitionProvider` 是一个 `UWorldSubsystem`（仅在游戏世界创建，包含 Dedicated Server），是运行期访问表现资产与媒体软引用异步预热的**唯一托管来源**：

```cpp
UCLASS()
class GAMEFEEDBACK_API UGameFeedbackDefinitionProvider : public UWorldSubsystem {
    GENERATED_BODY()

public:
    /** 按信号主通道与反馈标签同步解析最优资产指针 */
    const UGameFeedbackDefinition* Resolve(const FGameFeedbackSignal& Signal);

    /** O(1) 查询已登记 Definition 的稳定网络 ID（带版本校验的指针直查缓存） */
    uint32 GetDefinitionId(const UGameFeedbackDefinition* Definition) const;

    /** 获取当前 Registry 的确定性映射版本 */
    uint32 GetDefinitionMappingVersion() const;

    /** 按网络 ID 与映射版本还原服务端资产 */
    const UGameFeedbackDefinition* ResolveById(uint32 DefinitionId, uint32 DefinitionMappingVersion);

    /** 批量预热软引用定义资产集合（关卡加载或英雄入场时调用） */
    void PreloadFeedbackBundle(TConstArrayView<TSoftObjectPtr<UGameFeedbackDefinition>> Definitions);

    /** 按路径去重异步预热视听软资产（VFX / Audio），随世界生命周期保活并在注销时释放 */
    void PreloadSoftAsset(const FSoftObjectPath& AssetPath);

    /** 查询当前在飞异步预热句柄总数（供观测与测试） */
    int32 GetActivePreloadHandleCount() const;

    /** 安全获取当前世界的服务实例 */
    static UGameFeedbackDefinitionProvider* GetPtr(const UObject* WorldContextObject);
};
```

### 3.1 强缓存、同步完成竞态安全与句柄闭环管理
- **同步返回与 O(1) 缓存**：已解析定义存入 `LoadedDefinitions`，媒体软资产存入 `PreloadedMediaAssets`，并提供带 `CachedMappingVersion` 自动失效刷新的 `TObjectKey` 指针直查 `DefinitionIdCache`；
- **去重异步预热与同步完成处理**：Definition、Audio 与 VFX 软引用全部收口至本服务按 `FSoftObjectPath` 去重异步加载；若引擎在 `RequestAsyncLoad` 返回前即同步完成，立即直接入缓存返回而绝不残留已完成句柄；
- **注销安全清理**：在子系统 `Deinitialize` 时，自动遍历 `DefinitionLoads` 与 `PendingMediaLoads` 调用 `CancelHandle()` 并清空全部缓存，彻底杜绝异步加载句柄泄漏。
