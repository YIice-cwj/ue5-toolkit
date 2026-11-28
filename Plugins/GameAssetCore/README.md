# GameAssetCore

> **一句话**：以软引用为入口的异步资产流式加载、事件驱动 C++20 协程、真 GC 强引用常驻保活与蓝图异步动作基础设施。
> **版本** v2.0.0 · **引擎** UE 5.1 ~ 5.8 · **层级** Level 1 · **直接依赖** GameCore, GameLogging（GameLogging 为私有单向依赖）

## 30 秒速览

- **解决什么**：运行时按需把 `TSoftObjectPtr` / `TSoftClassPtr` 资产流式加载进内存，并在需要期间用 `FGCObject` 强引用阻止 GC 回收。
- **核心能力**：
  - 弱宿主流式加载：`LoadAsync` / `LoadClassAsync` / `LoadBatchAsync` / `LoadSync`；
  - 事件驱动 C++20 协程：`LoadAsyncCoro` / `LoadClassAsyncCoro` / `LoadBatchAsyncCoro`；
  - 强类型加载控制句柄：进度、取消、错误状态与批量结果提取；
  - `FGameAssetPinner` / `FScopedPinnedAsset` 引用计数与真 GC 常驻保活；
  - 蓝图异步节点与蓝图函数库。
- **什么时候用**：按需加载皮肤、模型、蓝图类或关卡道具，或需要跨场景常驻的资产。
- **什么时候不用**：资产在启动期一次性硬引用即可；需要 Actor / UObject 复用请用 `GamePool`，需要关卡流转与加载进度屏障请用 `GameFlow`。

## 快速开始

### 1. 引入模块

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core",
    "CoreUObject",
    "Engine",
    "GameCore",
    "GameAssetCore",
});
```

包含单个公共门面头 `#include "GameAssetCore.h"`，或按需包含子域头文件（如 `#include "Loader/FGameAssetLoader.h"`）。

### 2. 异步加载资产与类

```cpp
#include "Loader/FGameAssetLoader.h"

void AMyWeapon::EquipSkinAsync(const TSoftObjectPtr<UStaticMesh>& SkinMesh, const TSoftClassPtr<AActor>& MuzzleFxClass) {
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

    FGameAssetLoader::LoadClassAsync<AActor>(
        MuzzleFxClass,
        this,
        [this](TSubclassOf<AActor> LoadedClass) {
            if (LoadedClass != nullptr) {
                CachedMuzzleClass = LoadedClass;
            }
        }
    );
}
```

### 3. 作用域常驻保活

```cpp
#include "Pinning/FScopedPinnedAsset.h"

void UCombatVfxDirector::PlayTransientComboVfx(const TSoftObjectPtr<UObject>& VfxAsset) {
    FScopedPinnedAsset PinnedVfx(VfxAsset);
    if (UObject* AssetObj = PinnedVfx.Get()) {
        TriggerVfx(AssetObj);
    }
}
```

> 协程与批量加载、手工 `PinAsset` / `UnpinAsset` 成对管理等示例见 [QuickStart.md](Docs/Guides/QuickStart.md)。

## 核心能力

### 流式加载门面

- 单资产 / 类：`LoadAsync<TAsset>`、`LoadClassAsync<TClass>` 返回 `FGameAssetLoadHandle`，完成回调交付强类型指针或 `TSubclassOf<TClass>`。
- 批量：`LoadBatchAsync` 支持路径预热与强类型数组两种重载，内部做 $O(N)$ 路径去重，结果与输入顺序 1:1 对齐。
- 同步：`LoadSync<TAsset>` / `LoadClassSync<TClass>` 返回 `TGameStatusResult<T>`，已驻留时零流式开销直返。
- 类型与宿主安全：类加载强制 `LoadedClass->IsChildOf(TClass::StaticClass())`，过滤 `MarkAsGarbage` 濒死对象；`Owner` 为弱引用，宿主销毁后自动丢弃回调。

### 事件驱动 C++20 协程

- `LoadAsyncCoro` / `LoadClassAsyncCoro` / `LoadBatchAsyncCoro` 通过内部 `FStreamableAssetAwaiter` / `FStreamableBatchAwaiter` 直接挂钩 `FStreamableDelegate` 与 `FGameCoroSuspensionGuard`，零逐帧轮询、零 `UWorld` 子系统依赖。
- 目标资产或整个批次已驻留时走零挂起快路径，不创建流式句柄。
- `FCancellationTokenSource::Cancel()`、`Task.Cancel()` 与宿主销毁均立即触发底层 `CancelHandle()` 并安全销毁协程帧。
- 适用于无 `UWorld` 的 `UGameInstance` 或普通 `UObject` 宿主。

### 加载控制句柄

- `Cancel()` 立即释放底层资产并保留 `WasCanceled() == true` 可查；`Reset()` 仅释放保活引用，允许后续正常 GC。
- 状态查询：`IsValid()`、`IsLoading()`、`HasCompleted()`、`HasError()`、`GetProgress()`。
- 结果提取：`GetLoadedAsset()` / `GetLoadedAsset<T>()`，以及 `ForEachLoadedAsset` 与单遍零中间数组分配的 `GetLoadedAssets<T>`。

### GC 常驻保活

- `FPinnedAssetStorage` 使用 `std::construct_at` 原地构造，免疫静态析构顺序（SIOF）问题。
- 单一 `FGCObject` 收集器向引擎上报全部 `TObjectPtr<UObject>` 强引用，涵盖无磁盘包体的动态内存对象。
- `PinAsset` 直接返回 `TGameStatusResult<UObject*>`；`FScopedPinnedAsset` 在已驻留热路径仅需 1 次加锁即可缓存原生指针。
- 已驻留资产的增减计数与查询支持多线程；非主线程对未驻留资产请求同步加载返回 `InvalidThread (3005)`。

### 蓝图集成

- `UAsyncAction_LoadGameAsset` / `UAsyncAction_LoadGameAssetsBatch` 暴露 `LoadGameAssetAsync`、`LoadGameClassAsync`、`LoadGameAssetsBatchAsync`，提供 `OnSuccess` / `OnFailure` / `OnProgress` 引脚与 `Cancel()`。
- 蓝图节点委托 `FGameAssetLoader`，共享路径去重、类校验与 `OnProgress` 重入 `Cancel()` 防护，支持无 `UWorld` 的 `UGameInstance` 上下文。
- `UGameAssetBlueprintLibrary` 提供 `PinAsset` / `UnpinAsset` / `PinAssets` / `UnpinAssets`、引用计数查询与单次 `TSoftObjectPtr::Get()` 的零阻塞驻留查询。

### 错误码与日志

- 错误结果归属 `Game::ModuleId::Asset (3)`，定义于 [AssetErrorCodes.h](Source/GameAssetCore/Public/Core/AssetErrorCodes.h)：

| 错误码 | 标识符 | 含义与典型触发场景 |
|:---:|---|---|
| `3001` | `InvalidAssetPath` | 软引用或 `FSoftObjectPath` 为空或格式非法 |
| `3002` | `LoadFailed` | 磁盘包体不存在或 `FStreamableManager` 加载报错 |
| `3003` | `TypeMismatch` | 资产已加载但与目标 `TAsset` 或基类 `TClass`（`IsChildOf`）不兼容 |
| `3004` | `AssetNotPinned` | 对未处于常驻保活状态的资产调用 `UnpinAsset` |
| `3005` | `InvalidThread` | 在非 GameThread 工作线程对未驻留资产请求同步磁盘加载 |

- 日志经 `LogGameAssetCore` 输出：模块启停为 `Log`，流送失败 / 类型不匹配 / 非主线程冷加载为 `Warning`，流送与常驻生命周期为 `Verbose`。
- 驻留短路与高频引用计数热路径零日志输出。

## 架构与依赖

- **公开依赖**：`GameCore` 与引擎 `Core` / `CoreUObject` / `Engine`；**私有依赖**：`GameLogging`（`LogGameAssetCore`）。
- **横向耦合**：零业务插件与对象池耦合；模块类型 `Runtime`，`LoadingPhase` 为 `Default`。
- **关键类**：
  - `FGameAssetLoader`：静态加载门面（异步、协程、同步）；
  - `FGameAssetLoadHandle`：底层 `FStreamableHandle` 的强类型包装；
  - `FGameAssetPinner` / `FScopedPinnedAsset`：全局引用计数与 RAII 常驻保活；
  - `FAsyncRequestCancelBridge`：私有取消桥，原子隔离流式完成回调与 `FCancellationToken` 取消信号，完成后解绑令牌注册；
  - `UAsyncAction_*` / `UGameAssetBlueprintLibrary`：蓝图异步节点与函数库。

<details>
<summary><b>展开目录结构</b></summary>

```text
Plugins/GameAssetCore/
├── Docs/
│   ├── Architecture/Overview.md                # 架构拓扑、事件驱动协程与 FGCObject 保活设计
│   ├── API/AssetLoaderAndPinner.md             # 全量公共 API 与错误码契约手册
│   └── Guides/QuickStart.md                    # 接入示例与最佳实践指南
├── Source/
│   └── GameAssetCore/
│       ├── Public/
│       │   ├── Blueprint/
│       │   │   ├── AsyncAction_LoadGameAsset.h
│       │   │   ├── AsyncAction_LoadGameAssetsBatch.h
│       │   │   └── UGameAssetBlueprintLibrary.h
│       │   ├── Core/
│       │   │   ├── AssetErrorCodes.h           # Game::AssetErrorCodes (3001 ~ 3005)
│       │   │   └── GameAssetTypes.h            # EGameAssetPriority 与优先级转换
│       │   ├── Loader/
│       │   │   ├── FGameAssetLoadHandle.h
│       │   │   └── FGameAssetLoader.h
│       │   ├── Pinning/
│       │   │   ├── FGameAssetPinner.h
│       │   │   └── FScopedPinnedAsset.h
│       │   └── GameAssetCore.h                 # 模块入口与公共聚合门面头
│       ├── Private/
│       │   ├── Blueprint/
│       │   │   ├── AsyncAction_LoadGameAsset.cpp
│       │   │   ├── AsyncAction_LoadGameAssetsBatch.cpp
│       │   │   └── UGameAssetBlueprintLibrary.cpp
│       │   ├── Loader/
│       │   │   ├── FAsyncRequestCancelBridge.h
│       │   │   ├── FGameAssetLoadHandle.cpp
│       │   │   └── FGameAssetLoader.cpp
│       │   ├── Pinning/
│       │   │   ├── FPinnedAssetStorage.h
│       │   │   ├── FGameAssetPinner.cpp
│       │   │   └── FScopedPinnedAsset.cpp
│       │   ├── Tests/                          # 19 项自动化功能与微基准回归套件
│       │   │   ├── GameAssetBlueprintTest.cpp
│       │   │   ├── GameAssetCoroTest.cpp
│       │   │   ├── GameAssetLoaderTest.cpp
│       │   │   ├── GameAssetPerformanceTest.cpp
│       │   │   ├── GameAssetPinnerTest.cpp
│       │   │   ├── UGameAssetTestReceiver.cpp
│       │   │   └── UGameAssetTestReceiver.h
│       │   └── GameAssetCore.cpp
│       └── GameAssetCore.Build.cs
├── GameAssetCore.uplugin
└── README.md
```

</details>

## 性能与边界

<details>
<summary>展开性能基准（历史 Development 构建参考值）</summary>

实测平台：Apple Silicon（macOS arm64，UE 5.8 Development 构建，Clang 21.1.6，2 轮预热 + 7 轮采样中位数，来自 `GameAssetPerformanceTest.cpp`）：

| 基准项 | 中位数耗时 | 离散度 (CV%) | 吞吐量 | 核心机制收益 |
|---|---:|---:|---:|---|
| `FScopedPinnedAsset::Get<T>()` | `2.40 ns/op` | — | `417.39 Mops/s` | 栈上缓存原生指针，零锁零哈希直读 |
| `FGameAssetPinner::GetPinnedAsset` | `9.30 ns/op` | `0.03%` | `107.57 Mops/s` | 条目直接缓存 `TObjectPtr`，免跨模块句柄反查 |
| `PinAsset` + `UnpinAsset` 高频增减 | `22.00 ns/op` | `5.21%` | `45.45 Mops/s` | 命中直接返回缓存实例，热路径零日志干扰 |
| `UGameAssetBlueprintLibrary` 驻留查询 | `26.17 ns/op` | `5.55%` | `38.21 Mops/s` | 单次 `TSoftObjectPtr::Get()`，消除二次 `ResolveObject` 锁开销 |
| `FGameAssetLoader::LoadAsync` 驻留短路 | `29.10 ns/op` | `4.21%` | `34.36 Mops/s` | 零 `FStreamableHandle` 与零取消桥分配 |
| `FScopedPinnedAsset` 栈构造 + 析构 | `35.45 ns/op` | `3.78%` | `28.21 Mops/s` | 已驻留热路径构造由双次加锁优化为单次加锁 |
| `LoadAsyncCoro` 驻留快路径 | `121.90 ns/op` | `1.89%` | `8.20 Mops/s` | 含完整 C++20 协程状态机、`BindContext` 与零挂起直返 |
| `LoadBatchAsync` 16 路径全驻留扫描 | `1,696.02 ns/op` | `1.26%` | `589.62 Kops/s` | 全驻留快路径零 `TSet`/`TArray` 堆内存分配 |

</details>

- **线程**：已驻留资产的 Pin / Unpin / 查询支持多线程并发；非 GameThread 对未驻留资产请求同步加载返回 `InvalidThread (3005)`。
- **生命周期**：`FGameAssetPinner::Startup` / `Shutdown` 由模块自动调用；协程在 `BindContext` 宿主销毁或取消时销毁帧；`FScopedPinnedAsset` 析构自动递减引用计数。
- **内存**：`FPinnedAssetStorage` 通过单一 `FGCObject` 上报 `TObjectPtr<UObject>` 强引用，动态内存对象同样受保护。
- **Shipping**：日志受 `Game::Config::bEnableLog` 编译期开关控制；驻留短路与引用计数热路径零日志。
- **性能声明**：数值为 `GameAssetPerformanceTest.cpp` 的历史 Development 参考，未纳入 CI 自动复测，不构成 Shipping 性能承诺。回归命令 `python3 .scripts/run_automation_tests.py --case Game.Asset`。

## 文档导航

| 文档 | 内容 |
| :--- | :--- |
| [QuickStart.md](Docs/Guides/QuickStart.md) | 接入依赖、异步 / 协程 / 常驻保活示例与最佳实践 |
| [Overview.md](Docs/Architecture/Overview.md) | 架构拓扑、事件驱动协程与 FGCObject 保活设计 |
| [AssetLoaderAndPinner.md](Docs/API/AssetLoaderAndPinner.md) | 全量公共 API 与错误码契约手册 |
| [AssetErrorCodes.h](Source/GameAssetCore/Public/Core/AssetErrorCodes.h) | `Game::AssetErrorCodes` 3001 ~ 3005 常量定义 |
