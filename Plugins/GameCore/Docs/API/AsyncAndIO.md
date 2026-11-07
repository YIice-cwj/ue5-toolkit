# GameCore 异步任务、时间切片、资源加载、目录监听与关卡流转 API 参考

本文档汇总 `GameCore` 提供的五个跨系统异步与流转支撑设施：
1. `FGameThreadTaskRunner`：后台工作线程任务与回主线程安全续体
2. `UTimeSlicedTaskSubsystem`：主线程帧预算时间切片调度器（平摊重型主线程批处理防掉帧）
3. `FGameAssetLoader`：宿主弱引用安全软引用异步资源加载门面
4. `FDirectoryChangeWatcher`：通用目录时间戳轮询监听器
5. `ULoadingSubsystem` + `FLoadingBarrierToken`：关卡流转与异步业务就绪屏障调度子系统

---

## 1. `FGameThreadTaskRunner` (统一异步执行工具)

头文件：`#include "Tasks/FGameThreadTaskRunner.h"`

### 设计规范与红线
- 后台任务运行于工作线程（Work Thread），**严禁触碰任何 UObject、引擎 GC 状态或引擎单例**；
- 一切对 UObject 的读写操作必须放在 `GameThread` 续体内；
- 续体调度前强制校验 `TWeakObjectPtr<UObject> Owner` 的有效性。若宿主对象已提前销毁，续体直接丢弃不执行，彻底消灭野指针与生命周期竞态。

### 静态接口

```cpp
class GAMECORE_API FGameThreadTaskRunner {
public:
    FGameThreadTaskRunner() = delete;

    /**
     * @brief 派发后台工作线程任务
     */
    static void Run(TWeakObjectPtr<UObject> Owner, TFunction<void()> BackgroundWork);

    /**
     * @brief 回到 GameThread 主线程执行续体（宿主失效时安全丢弃）
     */
    static void RunOnGameThread(TWeakObjectPtr<UObject> Owner, TFunction<void()> GameThreadWork);

    /**
     * @brief 派发后台任务并在完成后将计算结果安全投递回 GameThread 续体执行
     * @tparam TResult 计算结果类型（支持移动语义）
     */
    template <typename TResult>
    static void RunWithContinuation(
        TWeakObjectPtr<UObject> Owner,
        TFunction<TResult()> BackgroundWork,
        TFunction<void(TResult)> GameThreadContinuation
    );

    /**
     * @brief 派发后台无返回值任务，并在完成后安全投递回 GameThread 续体执行
     */
    static void RunWithContinuation(
        TWeakObjectPtr<UObject> Owner,
        TFunction<void()> BackgroundWork,
        TFunction<void()> GameThreadContinuation
    );
};
```

### 经典使用示例（链式流式续体）
```cpp
void UMyDataLoader::StartAsyncProcessing()
{
    // 后台加载解析，并自动将数据带回 GameThread 消费（任意阶段宿主失效自动短路）
    FGameThreadTaskRunner::RunWithContinuation<TArray<uint8>>(
        this,
        []() {
            return HeavyDecompressFromDisk();
        },
        [this](TArray<uint8> ProcessedData) {
            this->ApplyDataToEngine(MoveTemp(ProcessedData));
        }
    );
}
```

---

## 2. `FDirectoryChangeWatcher` (通用目录变更监听器)

头文件：`#include "IO/FDirectoryChangeWatcher.h"`

### 职责与原理
- 通过 `FTSTicker` 周期扫描目标目录下文件的最近修改时间戳；
- 发现时间戳前进即递增变更序号，并通过 `FValueObserver<uint32>` 进行通知；
- 典型应用场景：静态数据（Excel/JSON/bin）热更新监听、Shader/材质变体开发监听。

### 接口定义

```cpp
class GAMECORE_API FDirectoryChangeWatcher {
public:
    FDirectoryChangeWatcher(
        const FName& WatcherName,
        const FName& EnabledConfigKey,
        const FString& DefaultRelativeDirectory
    );
    ~FDirectoryChangeWatcher();

    void Start();
    void Stop();

    [[nodiscard]] FValueObserver<uint32>& GetChangeObserver();
};
```

---

## 3. `ULoadingSubsystem` (关卡加载流转子系统)

头文件：`#include "System/ULoadingSubsystem.h"`  
基类：`UGameInstanceSubsystem`

### 职责与原理

- `Initialize` 中挂钩引擎全局委托 `FCoreUObjectDelegates::PreLoadMap` 与 `PostLoadMapWithWorld`，`Deinitialize` 中 `RemoveAll(this)` 注销；不依赖 World Tick。
- 由本子系统发起跳转时，先置位加载态并广播 `OnLoadingStateChanged(true, 目标关卡)`，再调用 `UGameplayStatics::OpenLevel`。
- 若地图加载由引擎其他途径触发（未经本子系统），`PreLoadMap` 回调会在未置位时自行置位并广播 `true`，保证遮罩不漏。

### 核心接口

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLoadingStateChanged, bool, bIsLoading, FName, TargetLevelName);

UCLASS(BlueprintType)
class GAMECORE_API ULoadingSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    /** 通过世界上下文（World / GameInstance）获取子系统，取不到返回 nullptr */
    UFUNCTION(BlueprintPure, Category = "GameCore|Loading", meta = (WorldContext = "WorldContextObject"))
    static ULoadingSubsystem* Get(const UObject* WorldContextObject);

    /** 加载状态变动广播（UI 订阅以显示/隐藏遮罩） */
    UPROPERTY(BlueprintAssignable, Category = "GameCore|Loading")
    FOnLoadingStateChanged OnLoadingStateChanged;

    /** 携带加载幕布事件的标准关卡跳转 */
    UFUNCTION(BlueprintCallable, Category = "GameCore|Loading")
    void OpenLevelWithLoading(const FName& LevelName);

    UFUNCTION(BlueprintPure, Category = "GameCore|Loading")
    bool IsLoading() const;

    UFUNCTION(BlueprintPure, Category = "GameCore|Loading")
    FName GetTargetLevelName() const;
};
```

### 行为契约

| 场景 | 实际行为 |
| :--- | :--- |
| `Get` 传入 `UGameInstance` | 直接取该 GameInstance 的 `ULoadingSubsystem` |
| `Get` 传入其他上下文 | 经 `GEngine->GetWorldFromContextObject(..., ReturnNull)` 取 World；World 或 GameInstance 为空返回 `nullptr` |
| 加载中再次调用 `OpenLevelWithLoading` | 记录 Warning 并忽略本次请求（不重复广播） |
| `LevelName` 为 `None` | 记录 Warning 并放弃，不置位加载态 |
| GameInstance 或当前 World 无效 | 记录 Warning 并放弃，不置位加载态 |
| 正常发起跳转 | 置位 `bIsLoading`、记录 `TargetLevelName`，广播 `(true, 关卡名)`，随后 `UGameplayStatics::OpenLevel` |
| `PostLoadMapWithWorld` 收到属于其他 GameInstance 的世界 | 直接返回，不误清本子系统的加载态 |
| 加载完成 | `bIsLoading` 复位 `false`，`TargetLevelName` 复位 `NAME_None`，广播 `(false, 完成时的关卡名)` |

### 典型用法

蓝图侧在 `BeginPlay` 绑定 `OnLoadingStateChanged` 控制遮罩显隐，跳转统一走 `OpenLevelWithLoading`；`IsLoading` / `GetTargetLevelName` 供纯查询与断线恢复判断。
在 C++ 中，若关卡加载完毕后还需等待对象池分帧预热或核心资产异步加载完成再揭开加载幕布，可调用 `TSharedRef<FLoadingBarrierToken> Token = LoadingSubsystem->AcquireLoadingBarrier(TEXT("PoolWarmup"))`：当 `PostLoadMap` 到达而仍存在未释放屏障时，幕布自动保持开启，直至所有屏障令牌调用 `Release()` 或离开作用域 RAII 析构后才统一广播 `OnLoadingStateChanged(false, LevelName)`。

---

## 4. `UTimeSlicedTaskSubsystem` (主线程帧预算时间切片执行器)

头文件：
- `#include "Tasks/UTimeSlicedTaskSubsystem.h"`
- `#include "Tasks/FTimeSlicedTaskHandle.h"`

基类：`UTickableWorldSubsystem`（当任务队列为空时 `IsTickable()` 返回 `false`，保持零空闲 Tick 损耗）

- `SetFrameBudgetMilliseconds(double BudgetMs)`：配置单帧最大执行预算（默认 `1.5ms`）。
- `EnqueueTask(Owner, StepWork)` / `EnqueueGlobalTask(StepWork)`：步进闭包返回 `true` 代表尚有后续工作留待下一帧，返回 `false` 代表完成。宿主 `Owner` 销毁时自动摘除跳过。
- `EnqueueBatch(Owner, TotalCount, PerItemWork, OnComplete)`：定长数组分帧批处理并在全部完成后回调 `OnComplete`。
- 返回 `FTimeSlicedTaskHandle`，支持随时调用 `Cancel()`（即使在步进回调内部自取消亦安全）。

---

## 5. `FGameAssetLoader` 与 `FScopedPinnedAsset` (安全软引用资产加载与内存保活门面)

头文件：
- `#include "Asset/FGameAssetLoader.h"`
- `#include "Asset/FGameAssetLoadHandle.h"`

封装引擎 `UAssetManager::GetStreamableManager()`，补齐宿主弱引用守卫、强类型类加载与内存保活原语：
- **异步安全加载**：
  - `LoadAsync<TAsset>(SoftAsset, Owner, OnLoaded, Priority)`：异步加载对象实例，常驻短路，宿主销毁自动防悬垂。
  - `LoadClassAsync<TClass>(SoftClass, Owner, OnLoaded, Priority)`：异步加载蓝图类或 C++ 派生类并解析 `UClass*`。
  - `LoadBatchAsync(AssetPaths, Owner, OnAllLoaded, Priority)`：批量异步加载一组软路径，过滤无效路径并提供批次通知。
- **同步阻塞降级**：
  - `LoadSync<TAsset>(SoftAsset)`：同步阻塞加载资产裸指针（常驻则直接返回）。
  - `LoadClassSync<TClass>(SoftClass)`：同步阻塞加载目标 `UClass*`。
- **内存保活与防 GC 误杀**：
  - `PinAsset(AssetPath)` / `UnpinAsset(AssetPath)`：引用计数管理底层流式句柄持有，杜绝未挂载被 GC 误杀。
  - `IsAssetPinned(AssetPath)` / `ClearAllPinnedAssets()`：状态查询与测试重置。
  - `FScopedPinnedAsset`：基于 RAII 的作用域保活句柄，离开作用域自动执行 `UnpinAsset`。
- 返回 `FGameAssetLoadHandle`，支持 `Cancel()`、`GetProgress()`、`IsLoading()` 与 `HasCompleted()`。若宿主 `Owner` 在磁盘 I/O 完成前已销毁，完成回调自动短路丢弃，杜绝野指针崩溃。

---

## 6. `UThrottledTickerSubsystem` (分级交错节流心跳子系统)

头文件：
- `#include "Tasks/UThrottledTickerSubsystem.h"`
- `#include "Tasks/FThrottledTickTypes.h"`

### 职责与原理
- 解决海量纯 C++ 实体、AI 决策、未挂载 Component 的轻量对象每帧 Tick 开销过大的痛点；
- 支持预设频率档位（`EveryFrame`、`Rate30Hz`、`Rate10Hz`、`Rate2Hz`、`Rate1Hz`）或自定义秒级间隔；
- **分相交错（Interleaved Phase Staggering）**：注册时自动计算相位偏移，将相同频率的心跳对象均匀打散到不同帧执行，彻底削峰平摊帧耗时；
- **宿主弱引用守卫**：宿主对象销毁时自动短路并不解引用，Tick 结束后自动安全剔除；
- **零空闲开销**：当没有注册任务时，`IsTickable()` 返回 `false` 自动休眠；
- 返回 `FThrottledTickHandle`，支持随时 `Unregister()`、`SetRate()` 或 `SetInterval()` 动态调频。

```cpp
UThrottledTickerSubsystem& Ticker = UThrottledTickerSubsystem::Get(GetWorld());

// 注册 10Hz 节流心跳（自动分相平摊，宿主销毁自动解挂）
FThrottledTickHandle Handle = Ticker.RegisterTick(
    this,
    EThrottledRate::Rate10Hz,
    [this](float AccumulatedDeltaTime) {
        this->PerformLowFrequencyUpdate(AccumulatedDeltaTime);
    }
);

// 动态调频或主动解挂
Handle.SetRate(EThrottledRate::Rate2Hz);
Handle.Unregister();
```


