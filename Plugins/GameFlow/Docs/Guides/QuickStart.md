# GameFlow 快速上手指南

## 1. 模块依赖接入

在业务模块的 `.Build.cs` 中添加 `"GameFlow"`：

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core",
    "CoreUObject",
    "Engine",
    "GameCore",
    "GameFlow",
});
```

---

## 2. 触发带幕布与遥测的关卡跳转

```cpp
#include "System/ULoadingSubsystem.h"

void OpenBattleLevel(const UObject* WorldContext) {
    if (ULoadingSubsystem* Loading = ULoadingSubsystem::Get(WorldContext)) {
        Loading->OpenLevelWithLoading(FName(TEXT("/Game/Maps/BattleArena")));
    }
}
```

---

## 3. 在异步子系统中使用零堆分配屏障令牌

适合对象池预热、大资源流式预载、网络握手等在地图加载完毕后仍需保持加载幕布的场景：

```cpp
#include "Async/FGameCoroAwaiters.h"
#include "Async/TGameCoroTask.h"
#include "System/FLoadingBarrierToken.h"
#include "System/ULoadingSubsystem.h"

TGameCoroTask<void> WarmupArenaAsync(UObject* Owner) {
    co_await Game::Coro::BindContext(Owner);

    ULoadingSubsystem* Loading = ULoadingSubsystem::Get(Owner);
    if (Loading == nullptr) {
        co_return;
    }

    // 栈上零堆分配 RAII 令牌：权重 2.0，单屏障独立看门狗超时 10 秒
    FLoadingBarrierToken Barrier = Loading->AcquireBarrierToken(
        FName(TEXT("ArenaWarmup")),
        2.0f,
        10.0f
    );

    Barrier.ReportProgress(0.5f, NSLOCTEXT("Loading", "WarmupMeshes", "正在预热战场资源..."));
    // ... 执行异步预热 ...
    Barrier.ReportProgress(1.0f, NSLOCTEXT("Loading", "WarmupDone", "战场资源就绪"));

    // 显式释放或在作用域退出时自动释放
    Barrier.Release();
}
```

> 若确需跨多路回调共享同一屏障所有权，可显式按需包装：`TSharedRef<FLoadingBarrierToken> Shared = MakeShared<FLoadingBarrierToken>(Loading->AcquireBarrierToken(...));`。

---

## 4. 使用 C++20 协程等待加载与全部屏障解除

```cpp
#include "Async/FCancellationTokenSource.h"
#include "Async/FGameCoroAwaiters.h"
#include "Async/FWaitLoadingCompleteAwaiter.h"

TGameCoroTask<void> StartIntroCutsceneAfterLoad(UObject* Host, FCancellationToken Token) {
    co_await Game::Coro::BindContext(Host, Token);

    // 统一签名：(WorldContextObject, CancellationToken, TimeoutSeconds)
    const bool bReady = co_await GameFlow::WaitLoadingComplete(Host, MoveTemp(Token), 15.0f);
    if (!bReady) {
        // 已被取消、超时或子系统销毁中止
        co_return;
    }

    // 此时地图与全部异步就绪屏障均已释放，且保证处于 GameThread
    PlayIntroCutscene(Host);
}
```

---

## 5. 蓝图监听与遥测订阅

1. **加载幕布 UI**：在 `UGameInstance` 或 HUD 初始化时绑定 `ULoadingSubsystem` 的 `OnLoadingStateChanged` 与 `OnLoadingProgressChanged` 控制遮罩开关与进度条。
2. **蓝图等待节点**：直接放置 `Wait For Loading Complete` 异步节点，分别连接 `OnCompleted`（正常进入玩法）与 `OnCancelled`（取消或中止收口）。
3. **性能遥测**：订阅 `OnLoadingTelemetryReport` 或读取 `GetLastTelemetryReport()` 获取 `TotalDurationMs`、`MapLoadDurationMs`、`bTimedOut` 及各屏障耗时明细。
