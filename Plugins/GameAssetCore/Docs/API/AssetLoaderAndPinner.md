# GameAssetCore 公共 API 参考手册

---

## 一、 错误码与基础类型 (`Public/Core/`)

### 1. `Game::AssetErrorCodes` (`AssetErrorCodes.h`)
全部错误均归属模块标识 `Game::ModuleId::Asset (3)`，配合 `TGameStatusResult<T>` 使用：

| 常量名 | 数值 | 触发条件 |
|---|:---:|---|
| `InvalidAssetPath` | `3001` | 传入的 `TSoftObjectPtr`、`TSoftClassPtr` 或 `FSoftObjectPath` 为空或格式非法 |
| `LoadFailed` | `3002` | 目标磁盘资产不存在或底层 `FStreamableManager` 加载报错 |
| `TypeMismatch` | `3003` | 资产已加载，但无法 `Cast<TAsset>` 或 `UClass` 不满足 `IsChildOf<TClass>()` |
| `AssetNotPinned` | `3004` | 对当前未处于常驻锁定状态的资产路径调用 `UnpinAsset` |
| `InvalidThread` | `3005` | 在非 `GameThread` 工作线程对尚未驻留内存的资产调用 `LoadSync` / `PinAsset` |

### 2. `EGameAssetPriority` (`GameAssetTypes.h`)
- `Low` (`-50`)：背景装饰、远景预加载资产；
- `Normal` (`0`)：常规场景与默认实体资产；
- `High` (`50`)：玩家当前交互与战斗即时表现资产；
- `Critical` (`100`)：关键阻断性 UI 或核心主实体资产。

---

## 二、 流式加载句柄与加载门面 (`Public/Loader/`)

### 1. `FGameAssetLoadHandle`
包装底层 `FStreamableHandle`，提供安全的生命周期控制与结果提取：
- `void Cancel()`：取消在途或活跃请求并立即释放底层资产引用，**保留 `WasCanceled() == true` 状态可查**，`IsValid()` 转为 `false`。
- `void Reset()`：释放底层保活句柄并清空内部共享指针，允许资产后续按常规 GC 回收。
- `bool IsValid() const noexcept` / `explicit operator bool() const noexcept`：关联请求处于活跃态（未取消且未释放）返回 `true`。
- `bool IsLoading() const` / `bool HasCompleted() const` / `bool HasError() const` / `bool WasCanceled() const`
- `float GetProgress() const`：返回 `[0.0f, 1.0f]` 进度（空/已取消/已释放句柄返回 `0.0f`，已完成返回 `1.0f`）。
- `UObject* GetLoadedAsset() const` / `template <typename T> T* GetLoadedAsset() const`
- `void ForEachLoadedAsset(TFunctionRef<void(UObject*)> Visitor) const`：零中间数组分配就地遍历有效资产。
- `void GetLoadedAssets(TArray<UObject*>& OutLoadedAssets) const` / `template <typename T> void GetLoadedAssets(TArray<T*>& OutLoadedAssets) const`

### 2. `FGameAssetLoader`
- **回调式异步加载**：
  - `LoadAsync<TAsset>(SoftAsset, Owner, OnLoaded, CancellationToken, Priority) -> FGameAssetLoadHandle`
  - `LoadClassAsync<TClass>(SoftClass, Owner, OnLoaded, CancellationToken, Priority) -> FGameAssetLoadHandle`（回调参数为 `TSubclassOf<TClass>`，兼容 `UClass*` 形参闭包）
  - `LoadBatchAsync(AssetPaths, Owner, OnAllLoaded, OnProgress, CancellationToken, Priority) -> FGameAssetLoadHandle`（无类型路径预热）
  - `LoadBatchAsync<TAsset>(SoftAssets, Owner, OnAllLoaded, OnProgress, CancellationToken, Priority) -> FGameAssetLoadHandle`（向回调交付顺序 1:1 对齐的 `const TArray<TAsset*>&`）
- **事件驱动 C++20 协程加载**（在父协程内 `co_await Load*Coro(...)` 解包返回 `TGameStatusResult<T>`；跨线程取消令牌触发时严格投递回 GameThread 唤醒父协程并销毁帧）：
  - `LoadAsyncCoro<TAsset>(SoftAsset, Owner, CancellationToken, Priority) -> TGameCoroTask<TAsset*>`
  - `LoadClassAsyncCoro<TClass>(SoftClass, Owner, CancellationToken, Priority) -> TGameCoroTask<TSubclassOf<TClass>>`
  - `LoadBatchAsyncCoro<TAsset>(SoftAssets, Owner, CancellationToken, Priority) -> TGameCoroTask<TArray<TAsset*>>`
- **同步阻塞加载**：
  - `LoadSync<TAsset>(SoftAsset) -> TGameStatusResult<TAsset*>`
  - `LoadClassSync<TClass>(SoftClass) -> TGameStatusResult<TSubclassOf<TClass>>`

---

## 三、 内存常驻保活与 RAII 守卫 (`Public/Pinning/`)

### 1. `FGameAssetPinner`
- `PinAsset(const FSoftObjectPath& AssetPath) -> TGameStatusResult<UObject*>`：锁定并递增常驻引用计数，直接返回已保活 `UObject*`。
- `UnpinAsset(const FSoftObjectPath& AssetPath) -> TGameStatusResult<void>`：递减常驻引用计数，归零时解除强引用。
- `IsAssetPinned` / `GetPinRefCount` / `GetPinnedAsset` / `GetPinnedAsset<T>` / `GetTotalPinnedCount` / `GetAllPinnedPaths` / `ClearAllPinnedAssets`。

### 2. `FScopedPinnedAsset`
- 支持由 `FSoftObjectPath`、`TSoftObjectPtr<T>`、`TSoftClassPtr<T>` 构造；支持移动构造/移动赋值与主动 `Release()`；
- `IsPinned()` / `explicit operator bool()` / `GetPath()` / `Get()` / `Get<T>()`（栈上缓存指针，$O(1)$ 零锁读取）。

---

## 四、 蓝图集成层 (`Public/Blueprint/`)

1. **`UAsyncAction_LoadGameAsset`**：暴露 `LoadGameAssetAsync` 与 `LoadGameClassAsync`（强制校验 `UClass`），提供 `OnSuccess`、`OnFailure`、`OnProgress` 动态引脚与 `Cancel()`（支持在 `OnProgress` 中安全重入取消）。
2. **`UAsyncAction_LoadGameAssetsBatch`**：暴露 `LoadGameAssetsBatchAsync`，内部自动 $O(N)$ 路径去重并按输入顺序还原输出 `TArray<UObject*>`。
3. **`UGameAssetBlueprintLibrary`**：提供 `PinAsset`、`UnpinAsset`、`PinAssets`、`UnpinAssets`、`IsAssetPinned`、`GetAssetPinRefCount`、`GetPinnedAsset`、`GetTotalPinnedAssetCount`、`GetAllPinnedAssetPaths`、`IsAssetLoaded`、`GetLoadedAsset`。
