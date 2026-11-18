# GameAssetCore 插件

**GameAssetCore** 是面向全项目的核心资产异步流式加载、弱宿主防悬垂与内存常驻（Pinning）保活基础设施插件。它为 UE5 提供强类型的安全加载契约、C++20 协程原语、进度通知及完备的防 GC 误杀与句柄管理机制。

---

## 一、 核心功能

1. **强类型异步流式加载 (`FGameAssetLoader`)**：
   - 封装引擎 `FStreamableManager`，提供 `LoadAsync<T>` 与 `LoadClassAsync<T>`；
   - **弱引用宿主守卫**：若宿主（`Owner`）在异步加载完成前已被销毁，自动短路丢弃回调，杜绝访问野指针崩溃；
   - **内存已驻留快速短路**：当目标资产已在内存中时，立即原地触发回调，无需无谓入队调度；
   - **C++20 协程加载原语**：提供 `LoadAsyncCoro<T>`、`LoadClassAsyncCoro<T>` 以及批量并发等待 `LoadBatchAsyncCoro<T>`，支持 `co_await` 挂起与恢复；
   - **批量加载与进度通知**：提供 `LoadBatchAsync`（同时支持 `TArray<FSoftObjectPath>` 与强类型 `TArray<TSoftObjectPtr<T>>` 模板重载），内部使用哈希集 $O(N)$ 快速去重与分帧进度广播。

2. **强类型加载句柄 (`FGameAssetLoadHandle`)**：
   - 包装底层 `FStreamableHandle`，提供加载状态观测（`IsValid`, `IsLoading`, `HasCompleted`, `HasError`, `WasCanceled`, `GetProgress`）；
   - 支持安全取消（`Cancel`）与优雅释放（`Reset`）；
   - 提供单资产与批量资产的强类型安全提取（`GetLoadedAsset<T>` / `GetLoadedAssets<T>`）。

3. **线程安全资产常驻保活 (`FGameAssetPinner`)**：
   - 为无强引用挂载的关键资产提供常驻内存保护（防止被引擎 GC 误杀回收）；
   - 内部由引用计数器驱动，支持多系统重复锁定，归零时自动释放流式句柄；
   - 采用细粒度锁与锁外 I/O 设计，杜绝在互斥锁临界区内执行同步磁盘加载造成的锁争用或死锁隐患；
   - 提供全局保活种类计数 `GetTotalPinnedCount`、常驻清单导出 `GetAllPinnedPaths` 与安全清理 `ClearAllPinnedAssets`。

4. **RAII 资产保活守卫 (`FScopedPinnedAsset`)**：
   - 栈上 RAII 作用域保活对象，离开作用域自动执行引用计数扣减与释放；
   - 构造成功后在本地缓存原生指针，后续解引用访问（`Get()` / `Get<T>()`）降为 $O(1)$ 裸指针读取，彻底避免重复哈希查询与锁竞争；
   - 完整支持移动语义（Move Constructible / Move Assignable）。

5. **蓝图异步加载动作节点 (`UAsyncAction_LoadGameAsset` / `UAsyncAction_LoadGameAssetsBatch`)**：
   - 向蓝图暴露单资产加载 `LoadGameAssetAsync`、单类加载 `LoadGameClassAsync` 与批量资产流送节点 `LoadGameAssetsBatchAsync`；
   - 提供 `OnSuccess`、`OnFailure`、`OnProgress` 三路动态多播执行引脚（批次节点支持返回 `TArray<UObject*>`），并同步提供原生 C++ 委托支持。

6. **蓝图资产函数库 (`UGameAssetBlueprintLibrary`)**：
   - 向蓝图开放常驻锁控制（`PinAsset` / `UnpinAsset` / `PinAssets` / `UnpinAssets` / `IsAssetPinned` / `GetPinnedAsset` / `GetAssetPinRefCount` / `GetAllPinnedAssetPaths`）；
   - 提供免阻塞快速状态查询（`IsAssetLoaded` / `GetLoadedAsset` / `GetTotalPinnedAssetCount`）。

---

## 二、 目录结构

```
Plugins/GameAssetCore/
├── Source/
│   └── GameAssetCore/
│       ├── Public/
│       │   ├── Asset/
│       │   │   ├── AsyncAction_LoadGameAsset.h
│       │   │   ├── AsyncAction_LoadGameAssetsBatch.h
│       │   │   ├── FGameAssetLoader.h
│       │   │   ├── FGameAssetLoadHandle.h
│       │   │   ├── FGameAssetPinner.h
│       │   │   ├── FScopedPinnedAsset.h
│       │   │   └── GameAssetTypes.h
│       │   ├── Library/
│       │   │   └── UGameAssetBlueprintLibrary.h
│       │   └── GameAssetCore.h
│       ├── Private/
│       │   ├── Asset/
│       │   │   ├── AsyncAction_LoadGameAsset.cpp
│       │   │   ├── AsyncAction_LoadGameAssetsBatch.cpp
│       │   │   ├── FGameAssetLoader.cpp
│       │   │   ├── FGameAssetLoadHandle.cpp
│       │   │   ├── FGameAssetPinner.cpp
│       │   │   └── FScopedPinnedAsset.cpp
│       │   ├── Library/
│       │   │   └── UGameAssetBlueprintLibrary.cpp
│       │   ├── Tests/
│       │   │   ├── AsyncAction_LoadGameAssetTest.cpp
│       │   │   ├── GameAssetLoaderTest.cpp
│       │   │   ├── GameAssetPerformanceTest.cpp
│       │   │   └── GameAssetPinnerTest.cpp
│       │   └── GameAssetCore.cpp
│       └── GameAssetCore.Build.cs
├── GameAssetCore.uplugin
└── README.md
```

---

## 三、 使用示例

### 1. 弱引用异步加载单个资产
```cpp
const TSoftObjectPtr<UStaticMesh> MeshPtr(FSoftObjectPath(TEXT("/Game/Meshes/SM_Weapon.SM_Weapon")));

FGameAssetLoadHandle Handle = FGameAssetLoader::LoadAsync<UStaticMesh>(
    MeshPtr,
    this, // 宿主指针（自动弱引用防野指针）
    [this](UStaticMesh* LoadedMesh) {
        if (LoadedMesh != nullptr) {
            WeaponMeshComponent->SetStaticMesh(LoadedMesh);
        }
    },
    FCancellationToken::None(),
    EGameAssetPriority::High
);
```

### 2. C++20 协程异步加载与批量等待
```cpp
// 单资产挂起加载
TGameCoroTask<void> AMyCharacter::SpawnSkillVfxCoro(TSoftObjectPtr<UParticleSystem> VfxPtr) {
    co_await Game::Coro::BindContext(this);

    UParticleSystem* Vfx = co_await FGameAssetLoader::LoadAsyncCoro(VfxPtr, this);
    if (Vfx != nullptr) {
        UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), Vfx, GetActorLocation());
    }
}

// 批量资产并发等待
TGameCoroTask<void> AMyCharacter::PreloadWeaponSetCoro(TArray<TSoftObjectPtr<UStaticMesh>> WeaponMeshes) {
    co_await Game::Coro::BindContext(this);

    TArray<UStaticMesh*> LoadedMeshes = co_await FGameAssetLoader::LoadBatchAsyncCoro(WeaponMeshes, this);
    for (UStaticMesh* Mesh : LoadedMeshes) {
        // 使用加载完成的静态网格体...
    }
}
```

### 3. RAII 资产内存保活
```cpp
{
    // 进入作用域锁定常驻，防 GC 误杀回收
    FScopedPinnedAsset PinnedBuffVfx(BuffVfxSoftPtr);
    if (UParticleSystem* Particle = PinnedBuffVfx.Get<UParticleSystem>()) {
        // 使用资产...
    }
} // 离开作用域自动扣减引用计数，归零时释放
```
