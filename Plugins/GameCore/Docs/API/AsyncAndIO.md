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

### 核心接口

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLoadingStateChanged, bool, bIsLoading, FName, TargetLevelName);

UCLASS(BlueprintType)
class GAMECORE_API ULoadingSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    /** 关卡加载状态变动委托（UI 订阅此事件显示/隐藏 Loading 遮罩） */
    UPROPERTY(BlueprintAssignable, Category = "Game|Loading")
    FOnLoadingStateChanged OnLoadingStateChanged;

    /** 平滑打开指定关卡 */
    UFUNCTION(BlueprintCallable, Category = "Game|Loading")
    void OpenLevel(FName LevelName);

    /** 跳转至配置中的初始关卡（Levels.Start） */
    UFUNCTION(BlueprintCallable, Category = "Game|Loading")
    void OpenStartLevel();

    /** 跳转至配置中的主菜单关卡（Levels.MainMenu） */
    UFUNCTION(BlueprintCallable, Category = "Game|Loading")
    void OpenMainMenuLevel();

    UFUNCTION(BlueprintPure, Category = "Game|Loading")
    bool IsLoading() const;
};
```

