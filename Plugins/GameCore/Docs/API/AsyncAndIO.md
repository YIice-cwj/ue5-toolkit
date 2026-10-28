# GameCore 异步任务、目录监听与关卡流转 API 参考

本文档汇总 `GameCore` 提供的三个跨系统支撑设施：
1. `FGameThreadTaskRunner`：后台任务与回主线程安全续体
2. `FDirectoryChangeWatcher`：通用目录时间戳轮询监听器
3. `ULoadingSubsystem`：关卡流转与加载遮罩调度子系统

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

