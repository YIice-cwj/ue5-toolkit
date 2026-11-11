# GameCore 快速上手指南

本指南将带您在 10 分钟内完成 `GameCore` 在新项目或新模块中的接入与核心原语实战。

---

## 1. 添加模块引用

在您自定义插件或项目的 `*.Build.cs` 中添加 `GameCore` 模块依赖：

```csharp
public class MyGameModule : ModuleRules
{
    public MyGameModule(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "GameplayTags",
            "GameCore", // 引入核心底层库
        });
    }
}
```

---

## 2. 常用功能代码集成示例

### 场景 A：使用编译期开关控制调试代码（零指令开销）
```cpp
#include "Config/GameCompileSwitches.h"

void AMyCharacter::DebugDrawCombatBounds()
{
    // 在编译期若关闭 DebugDraw（如 Shipping 模式），此处代码在编译优化阶段完全抹除，零汇编开销
    if constexpr (Game::Config::bEnableDebugDraw)
    {
        DrawDebugSphere(GetWorld(), GetActorLocation(), 100.0f, 16, FColor::Red);
    }
}
```

### 场景 B：注册与监听运行时全局配置 (`FConfigRegistry`)
```cpp
#include "Config/FConfigRegistry.h"

void UMyGameInstance::Init()
{
    Super::Init();

    // 1. 注册默认配置
    FConfigRegistry::Get().RegisterConfig(TEXT("Game.MaxPlayers"), 8);

    // 2. 响应式监听配置变更（返回 RAII 守卫自动注销）
    ConfigSub = FConfigRegistry::Get().WatchConfig<int32>(
        TEXT("Game.MaxPlayers"),
        [this](const int32& NewLimit) {
            UE_LOG(LogTemp, Log, TEXT("玩家上限调整为: %d"), NewLimit);
        }
    );
}
```

### 场景 C：使用 TSlotMap 管理海量纯 C++ 实体与连续遍历
```cpp
#include "Containers/TSlotMap.h"

struct FBulletData {
    FVector Position;
    FVector Velocity;
};

// 声明强类型句柄并实例化槽位表
struct FBulletTag {};
using FBulletHandle = TGameHandle<FBulletTag>;
TSlotMap<FBulletData, FBulletTag> Bullets;

// 1. O(1) 分配实体并返回安全代际句柄（代际号防 ABA 悬垂）
FBulletHandle BulletHandle = Bullets.Emplace(FVector::ZeroVector, FVector(100.0f, 0.0f, 0.0f));

// 2. 密集连续内存遍历（CPU L1/L2 缓存友好）
Bullets.ForEach([](FBulletHandle Handle, FBulletData& Bullet) {
    Bullet.Position += Bullet.Velocity * 0.016f;
    return true;
});
```

### 场景 D：纯 C++ 依赖倒置与服务定位 (`FGameServiceLocator`)
```cpp
#include "Service/FGameServiceLocator.h"

// 1. 定义纯 C++ 抽象业务接口
struct IAudioEngineService {
    virtual ~IAudioEngineService() = default;
    virtual void PlaySound(const FString& SoundPath) = 0;
};

// 2. 在模块初始化时注入服务实现
FGameServiceLocator::Get().RegisterService<IAudioEngineService>(
    MakeShared<FConcreteAudioEngine, ESPMode::ThreadSafe>()
);

// 3. 在任意线程/模块中优雅解析
if (TSharedPtr<IAudioEngineService> Audio = FGameServiceLocator::Get().Resolve<IAudioEngineService>()) {
    Audio->PlaySound(TEXT("Explosion.wav"));
}
```

### 场景 E：C++20 协程与主线程安全调度 (`TGameCoroTask`)
```cpp
#include "Async/TGameCoroTask.h"
#include "Async/FGameCoroAwaiters.h"

TGameCoroTask<int32> AMyActor::CalculateAndApplyAsync(FCancellationToken Token)
{
    // 绑定宿主生命周期与取消令牌：若 Actor 提前销毁则在挂起点自动自毁协程帧，绝不 UAF
    co_await Game::Coro::BindContext(this, Token);

    // 1. 切换至后台工作线程执行重型计算
    const int32 ComputedResult = co_await Game::Coro::RunOnBackground<int32>([]() {
        return PerformHeavyPathfindingOrDecompression();
    });

    // 2. 安全跨帧等待下一帧主线程
    co_await Game::Coro::NextFrame(this);

    // 3. 安全写回 Actor 状态
    this->ApplyResult(ComputedResult);
    co_return ComputedResult;
}
```

### 场景 F：高精度 CPU 作用域微秒采样 (`GAME_SCOPED_CPU_TIMER`)
```cpp
#include "Profiling/FScopedCpuTimer.h"

void AMyActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 非 Shipping 下统计耗时，Shipping 下完全擦除为零开销
    GAME_SCOPED_CPU_TIMER(Timer, [](double ElapsedUs) {
        if (ElapsedUs > 1000.0) {
            UE_LOG(LogTemp, Warning, TEXT("AMyActor Tick 耗时超时: %.2f us"), ElapsedUs);
        }
    });

    // 业务逻辑...
}
```

---

## 3. 完整 API 规范索引

各组件深度设计与 API 清单请查阅专属技术文档：
- **Config**: [ConfigRegistry.md](../API/ConfigRegistry.md)
- **Types**: [RandomAndTypes.md](../API/RandomAndTypes.md)
- **Containers**: [Containers.md](../API/Containers.md)
- **Memory**: [Memory.md](../API/Memory.md)
- **Determinism**: [Determinism.md](../API/Determinism.md)
- **Random**: [Random.md](../API/Random.md)
- **State**: [State.md](../API/State.md)
- **Math**: [Math.md](../API/Math.md)
- **Listener**: [ListenerHub.md](../API/ListenerHub.md)
- **Cache**: [Cache.md](../API/Cache.md)
- **Async**: [Async.md](../API/Async.md)
- **Profiling & Service**: [ProfilingAndService.md](../API/ProfilingAndService.md)
