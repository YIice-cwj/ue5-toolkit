# GameAssetCore API 参考

> **一句话**：资产异步加载、取消、GC 常驻保活与蓝图入口的公共 API。
> **头文件**：`GameAssetCore.h`（聚合入口）或按子域包含 · **线程**：同步加载与 Pin 仅限 GameThread；异步回调与协程恢复在 GameThread · **失败语义**：`TGameStatusResult<T>` + `Game::AssetErrorCodes`

## 30 秒速览

- **加载**：`FGameAssetLoader` 提供回调式 `LoadAsync` / `LoadClassAsync` / `LoadBatchAsync`、协程式 `*Coro` 与同步 `LoadSync` / `LoadClassSync`。
- **控制**：`FGameAssetLoadHandle` 负责取消、进度查询与结果提取；`Cancel()` 后 `IsValid()` 立即为 `false`。
- **保活**：`FGameAssetPinner` 手工增减常驻计数；`FScopedPinnedAsset` 提供栈上 RAII 守卫。
- **蓝图**：`UAsyncAction_LoadGameAsset` / `UAsyncAction_LoadGameAssetsBatch` / `UGameAssetBlueprintLibrary`。
- **约束**：非 GameThread 对未驻留资产执行同步加载会返回 `InvalidThread (3005)`。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FGameAssetLoader::LoadAsync` | 回调式异步加载资产 / 类 | 回调在 GameThread；Owner 失效自动丢弃 |
| `FGameAssetLoader::LoadAsyncCoro` | 协程式加载，`co_await` 解包结果 | 取消回投 GameThread 恢复 |
| `FGameAssetLoader::LoadSync` | GameThread 同步加载 | 非 GameThread 冷加载返回 `3005` |
| `FGameAssetLoadHandle::Cancel` | 取消请求并释放引用 | 之后 `IsValid() == false`、`WasCanceled() == true` |
| `FGameAssetLoadHandle::GetProgress` | 查询 `[0,1]` 进度 | 空 / 取消返回 `0`，完成返回 `1` |
| `FGameAssetPinner::PinAsset` | 常驻计数 +1，返回对象 | 冷加载仅 GameThread |
| `FGameAssetPinner::UnpinAsset` | 常驻计数 -1，归零解绑 | 未锁定返回 `3004` |
| `FScopedPinnedAsset` | 栈上 RAII 常驻守卫 | `Get()` 为 O(1) 零锁读取 |
| `UAsyncAction_LoadGameAsset` | 蓝图异步加载节点 | 支持在 `OnProgress` 中重入 `Cancel()` |
| `UGameAssetBlueprintLibrary` | 蓝图常驻查询与批量管理 | 空路径返回 `3001` |

## 错误码

全部错误归属 `Game::ModuleId::Asset (3)`，配合 `TGameStatusResult<T>` 使用。

| 常量 | 数值 | 触发条件 |
| :--- | :---: | :--- |
| `InvalidAssetPath` | `3001` | 传入的 `TSoftObjectPtr`、`TSoftClassPtr` 或 `FSoftObjectPath` 为空或格式非法 |
| `LoadFailed` | `3002` | 目标磁盘资产不存在，或底层 `FStreamableManager` 加载报错 |
| `TypeMismatch` | `3003` | 资产已加载，但无法 `Cast<TAsset>`，或 `UClass` 不满足 `IsChildOf(TClass::StaticClass())` |
| `AssetNotPinned` | `3004` | 对当前未处于常驻锁定状态的资产路径调用 `UnpinAsset` |
| `InvalidThread` | `3005` | 在非 GameThread 对尚未驻留内存的资产调用 `LoadSync` / `PinAsset` |

## 优先级

`EGameAssetPriority` 控制流式加载优先级：

| 枚举值 | 数值 | 典型用途 |
| :--- | :---: | :--- |
| `Low` | `-50` | 背景装饰、远景预加载 |
| `Normal` | `0` | 常规场景与默认实体 |
| `High` | `50` | 玩家当前交互与战斗即时表现 |
| `Critical` | `100` | 关键阻断 UI 或核心主实体 |

## 加载句柄 `FGameAssetLoadHandle`

包装底层 `FStreamableHandle`，提供安全的生命周期控制与结果提取。

| 方法 | 说明 |
| :--- | :--- |
| `Cancel()` | 取消在途或活跃请求并立即释放底层资产引用；保留 `WasCanceled() == true`，`IsValid()` 转为 `false` |
| `Reset()` | 释放保活句柄并清空共享指针，资产可按常规 GC 回收 |
| `IsValid()` / `operator bool` | 关联请求处于活跃态（未取消且未释放）返回 `true` |
| `IsLoading()` / `HasCompleted()` / `HasError()` / `WasCanceled()` | 状态查询 |
| `GetProgress()` | 返回 `[0, 1]`；空 / 已取消 / 已释放返回 `0`，完成返回 `1` |
| `GetLoadedAsset()` / `GetLoadedAsset<T>()` | 取首个加载结果 |
| `ForEachLoadedAsset(Visitor)` | 零中间数组分配就地遍历 |
| `GetLoadedAssets(Out)` / `GetLoadedAssets<T>(Out)` | 收集全部结果 |

## 加载门面 `FGameAssetLoader`

### 回调式异步加载

```cpp
template <typename TAsset>
static FGameAssetLoadHandle LoadAsync(
    const TSoftObjectPtr<TAsset>& SoftAsset,
    TWeakObjectPtr<UObject> Owner,
    TFunction<void(TAsset*)> OnLoaded,
    const FCancellationToken& CancellationToken = FCancellationToken::None(),
    EGameAssetPriority Priority = EGameAssetPriority::Normal
);
```

`LoadClassAsync<TClass>` 回调参数为 `TSubclassOf<TClass>`；`LoadBatchAsync` 支持无类型路径预热与强类型批量加载。

### 协程式加载

```cpp
template <typename TAsset>
static TGameCoroTask<TAsset*> LoadAsyncCoro(
    const TSoftObjectPtr<TAsset>& SoftAsset,
    TWeakObjectPtr<UObject> Owner,
    const FCancellationToken& CancellationToken = FCancellationToken::None(),
    EGameAssetPriority Priority = EGameAssetPriority::Normal
);
```

在父协程内 `co_await` 解包为 `TGameStatusResult<T>`；跨线程取消令牌触发时投递回 GameThread 唤醒父协程并销毁帧。另有 `LoadClassAsyncCoro` 与 `LoadBatchAsyncCoro`。

### 同步加载

```cpp
template <typename TAsset>
static TGameStatusResult<TAsset*> LoadSync(const TSoftObjectPtr<TAsset>& SoftAsset);

template <typename TClass>
static TGameStatusResult<TSubclassOf<TClass>> LoadClassSync(const TSoftClassPtr<TClass>& SoftClass);
```

- 仅限 GameThread；非 GameThread 对未驻留资产调用会返回 `InvalidThread (3005)`。
- `LoadClassSync` 强制校验继承契约，失败返回 `TypeMismatch (3003)`。

### 最小示例

```cpp
SkinLoadHandle = FGameAssetLoader::LoadAsync<UStaticMesh>(
    SkinMesh,
    this,
    [this](UStaticMesh* LoadedMesh) {
        if (LoadedMesh != nullptr) {
            WeaponMeshComp->SetStaticMesh(LoadedMesh);
        }
    },
    FCancellationToken::None(),
    EGameAssetPriority::High
);
```

## 常驻保活

### `FGameAssetPinner`

| 方法 | 说明 |
| :--- | :--- |
| `PinAsset(Path) -> TGameStatusResult<UObject*>` | 锁定并递增常驻引用计数，返回已保活对象 |
| `UnpinAsset(Path) -> TGameStatusResult<void>` | 递减计数，归零时解除强引用；未锁定返回 `AssetNotPinned (3004)` |
| `IsAssetPinned` / `GetPinRefCount` / `GetPinnedAsset` / `GetPinnedAsset<T>` | 查询 |
| `GetTotalPinnedCount` / `GetAllPinnedPaths` / `ClearAllPinnedAssets` | 统计与清理 |

### `FScopedPinnedAsset`

```cpp
void UCombatVfxDirector::PlayTransientComboVfx(const TSoftObjectPtr<UObject>& VfxAsset) {
    FScopedPinnedAsset PinnedVfx(VfxAsset);
    if (UObject* AssetObj = PinnedVfx.Get()) {
        TriggerVfx(AssetObj);
    }
}
```

- 支持 `FSoftObjectPath`、`TSoftObjectPtr<T>`、`TSoftClassPtr<T>` 构造，支持移动与主动 `Release()`。
- `Get()` / `Get<T>()` 为栈上缓存指针，O(1) 零锁读取。

## 蓝图层

| 类型 | 能力 |
| :--- | :--- |
| `UAsyncAction_LoadGameAsset` | `LoadGameAssetAsync` / `LoadGameClassAsync`；`OnSuccess`、`OnFailure`、`OnProgress` 引脚；支持在 `OnProgress` 中重入 `Cancel()` |
| `UAsyncAction_LoadGameAssetsBatch` | `LoadGameAssetsBatchAsync`；O(N) 路径去重，按输入顺序还原输出 |
| `UGameAssetBlueprintLibrary` | `PinAsset` / `UnpinAsset` / `PinAssets` / `UnpinAssets`、常驻查询、`IsAssetLoaded` / `GetLoadedAsset` |

## 常见错误与边界

- **空路径**：所有入口先校验路径，空或非法返回 `InvalidAssetPath (3001)`。
- **类型不匹配**：类加载统一用 `IsChildOf(TClass::StaticClass())` 校验，失败返回 `TypeMismatch (3003)`。
- **取消语义**：`Cancel()` 后句柄 `IsValid() == false` 且 `WasCanceled() == true`；已完成请求取消不会回滚。
- **宿主生命周期**：`Owner` 为弱引用，宿主销毁后回调自动丢弃，不触发 UAF。
- **线程边界**：异步完成回调与协程恢复均在 GameThread；同步加载与 Pin 的冷路径仅允许 GameThread。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [快速上手](../Guides/QuickStart.md)
