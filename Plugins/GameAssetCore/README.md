# GameAssetCore 插件

**GameAssetCore**（v2.0.0）是面向 Unreal Engine 5（UE 5.1 ~ 5.8）的高性能异步资产流式加载、事件驱动 C++20 协程原语、真 GC 强引用常驻保活（Pinning）与蓝图异步动作基础设施插件。它仅依赖底层 `GameCore`、`GameLogging` 与引擎基础模块，零横向业务与对象池耦合。

---

## 一、 核心功能与架构特性

1. **强类型弱宿主流式加载 (`FGameAssetLoader`)**
   - 封装引擎 `FStreamableManager`，提供 `LoadAsync<TAsset>`、`LoadClassAsync<TClass>`、`LoadBatchAsync` 与同步加载 `LoadSync<TAsset>`、`LoadClassSync<TClass>`；
   - **严格类型与继承契约**：类加载统一交付 `TSubclassOf<TClass>` 并强制校验 `IsChildOf<TClass>()`，过滤 `MarkAsGarbage` 濒死对象，同步加载严格区分 `LoadFailed (3002)` 与 `TypeMismatch (3003)`；
   - **双向竞态安全取消**：内部 `FAsyncRequestCancelBridge` 原子隔离流式同步完成回调与 `FCancellationToken` 并发取消信号，完成后即时解绑令牌注册防泄漏。

2. **事件驱动 C++20 协程加载 (`LoadAsyncCoro` / `LoadClassAsyncCoro` / `LoadBatchAsyncCoro`)**
   - **零逐帧轮询、零 `UWorld` 子系统依赖**：通过私有 `FStreamableAssetAwaiter` / `FStreamableBatchAwaiter` 直接挂钩 `FStreamableDelegate` 与 `GameCore::FGameCoroSuspensionGuard`，即便宿主是无 `UWorld` 的 `UGameInstance` 或 `UObject` 亦可单次精确唤醒；
   - **驻留快路径**：目标资产或整个批次已驻留内存时零挂起、零流式句柄分配直返；
   - **全通道即时取消**：`FCancellationToken::Cancel()`、`Task.Cancel()` 与宿主销毁均立即触发底层 `CancelHandle()` 并安全销毁协程帧。

3. **强类型流式控制句柄 (`FGameAssetLoadHandle`)**
   - 提供 `Cancel()`（立即释放底层资产且保持 `WasCanceled() == true` 状态可查）、`Reset()`、`IsValid()`、`IsLoading()`、`HasCompleted()`、`HasError()`、`GetProgress()`；
   - 提供 `ForEachLoadedAsset` 与单遍零中间数组分配的 `GetLoadedAssets<T>`。

4. **SIOF 免疫与 `FGCObject` 强引用常驻保活 (`FGameAssetPinner` & `FScopedPinnedAsset`)**
   - **真 GC 保活**：底层 `FPinnedAssetStorage` 采用 `std::construct_at` 免疫静态析构顺序灾难，并通过单一 `FGCObject` 收集器向引擎 GC 直接上报全部 `TObjectPtr<UObject>` 强引用（涵盖无磁盘包体的动态内存对象）；
   - **单次加锁构造与零锁读取**：`PinAsset` 直接返回 `TGameStatusResult<UObject*>`，`FScopedPinnedAsset` 构造仅需 1 次加锁即可缓存原生指针，`Get()` / `Get<T>()` 降为 `2.40 ns/op` 栈指针直读；
   - **线程边界防护**：已驻留资产的增减计数与查询完全支持多线程并发；非主线程请求冷磁盘同步加载时安全拦截并返回 `InvalidThread (3005)`。

5. **纯净蓝图集成层 (`UAsyncAction_LoadGameAsset` / `UAsyncAction_LoadGameAssetsBatch` / `UGameAssetBlueprintLibrary`)**
   - 蓝图异步节点全面委托 `FGameAssetLoader`，共享 $O(N)$ 路径去重、类校验与 `OnProgress` 重入 `Cancel()` 防护，支持直接挂载无 `UWorld` 的 `UGameInstance` 上下文；
   - `UGameAssetBlueprintLibrary` 提供常驻引用计数控制与单次 `TSoftObjectPtr::Get()` 零阻塞快速驻留查询。

6. **可观测日志体系 (`LogGameAssetCore`)**
   - 私有单向接入 `GameLogging`，模块启停输出 `Log` 里程碑，流送失败与类型不匹配输出 `Warning`，流送/常驻生命周期输出 `Verbose`，驻留短路与高频引用计数热路径 100% 静默。

---

## 二、 目录结构

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

---

## 三、 错误码规范 (`Game::AssetErrorCodes`)

全部错误结果均归属 `Game::ModuleId::Asset (3)`，定义于 [AssetErrorCodes.h](Source/GameAssetCore/Public/Core/AssetErrorCodes.h)：

| 错误码 | 标识符 | 含义与典型触发场景 |
|:---:|---|---|
| `3001` | `InvalidAssetPath` | 软引用或 `FSoftObjectPath` 为空或格式非法 |
| `3002` | `LoadFailed` | 磁盘包体不存在或 `FStreamableManager` 加载报错 |
| `3003` | `TypeMismatch` | 资产已加载但与目标 `TAsset` 或基类 `TClass`（`IsChildOf`）不兼容 |
| `3004` | `AssetNotPinned` | 对未处于常驻保活状态的资产调用 `UnpinAsset` |
| `3005` | `InvalidThread` | 在非 GameThread 工作线程对未驻留资产请求同步磁盘加载 |

---

## 四、 性能基准

实测平台：Apple Silicon（macOS arm64，UE 5.8 Development 构建，Clang 21.1.6，2 轮预热 + 7 轮采样中位数，来自 `GameAssetPerformanceTest.cpp`）：

| 基准项 | 中位数耗时 | 离散度 (CV%) | 吞吐量 | 核心机制收益 |
|---|---:|---:|---:|---|
| `FScopedPinnedAsset::Get<T>()` | `2.40 ns/op` | — | `417.39 Mops/s` | 栈上缓存原生指针，零锁零哈希直读 |
| `FGameAssetPinner::GetPinnedAsset` | `9.30 ns/op` | `0.03%` | `107.57 Mops/s` | 条目直接缓存 `TObjectPtr`，免跨模块句柄反查 |
| `PinAsset` + `UnpinAsset` 高频增减 | `22.00 ns/op` | `5.21%` | `45.45 Mops/s` | 命中直接返回缓存实例，热路径零日志干扰 |
| `UGameAssetBlueprintLibrary` 驻留查询 | `26.17 ns/op` | `5.55%` | `38.21 Mops/s` | 单次 `TSoftObjectPtr::Get()`，消除二次 `ResolveObject` 锁开销 |
| `FGameAssetLoader::LoadAsync` 驻留短路 | `29.10 ns/op` | `4.21%` | `34.36 Mops/s` | 零 `FStreamableHandle` 与零取消桥分配 |
| `FScopedPinnedAsset` 栈构造 + 析构 | `35.45 ns/op` | `3.78%` | `28.21 Mops/s` | 构造期由双次加锁优化为单次加锁 |
| `LoadAsyncCoro` 驻留快路径 | `121.90 ns/op` | `1.89%` | `8.20 Mops/s` | 含完整 C++20 协程状态机、`BindContext` 与零挂起直返 |
| `LoadBatchAsync` 16 路径全驻留扫描 | `1,696.02 ns/op` | `1.26%` | `589.62 Kops/s` | 全驻留快路径零 `TSet`/`TArray` 堆内存分配 |

---

## 五、 自动化测试与延伸文档

```bash
python3 .scripts/run_automation_tests.py --case Game.Asset
```

- [架构概述与核心机制](Docs/Architecture/Overview.md)
- [公共 API 参考手册](Docs/API/AssetLoaderAndPinner.md)
- [快速上手与最佳实践指南](Docs/Guides/QuickStart.md)
