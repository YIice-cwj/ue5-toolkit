# GameCore 快速上手指南

本指南将带您在 10 分钟内完成 `GameCore` 在新项目或新模块中的接入与核心功能验证。

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

### 场景 A：使用编译期开关控制调试代码
```cpp
#include "Config/GameCompileSwitches.h"

void AMyCharacter::DebugDrawCombatBounds()
{
    // 在编译期若关闭 DebugDraw（如 Shipping 模式），此处代码完全不生成汇编指令
    if constexpr (Game::Config::bEnableDebugDraw)
    {
        DrawDebugSphere(GetWorld(), GetActorLocation(), 100.0f, 16, FColor::Red);
    }
}
```

### 场景 B：注册游戏运行时全局配置
```cpp
#include "Config/FConfigRegistry.h"

void UMyGameInstance::Init()
{
    Super::Init();

    // 注册全局默认配置
    Game::Config::FConfigRegistry::Get().RegisterDefault(TEXT("Game.MaxPlayers"), 8);
    Game::Config::FConfigRegistry::Get().RegisterDefault(TEXT("Game.Difficulty"), 1.5f);
}
```

### 场景 C：在 Actor 中使用对象池
```cpp
#include "Pool/ObjectPoolSubsystem.h"

void AEnemySpawner::SpawnEnemy()
{
    UObjectPoolSubsystem& Pool = UObjectPoolSubsystem::Get(GetWorld());

    // 借出或复用敌人（若池中无空闲实例则自动 Spawn）
    AActor* Enemy = Pool.AcquireActor(EnemyClass, SpawnPointTransform);
}

void AEnemyCharacter::OnDie()
{
    // 归还至对象池休眠
    UObjectPoolSubsystem::Get(GetWorld()).ReleaseActor(this);
}
```

### 场景 D：受击瞬间顿帧（HitStop）
```cpp
#include "Time/TimeDilationSubsystem.h"

void AMyWeapon::OnHit(AActor* Victim)
{
    // 受击者受击定格 0.06 秒（约 3~4 帧）营造强烈的打击顿挫感
    UTimeDilationSubsystem::Get(GetWorld()).RequestHitStop(Victim, 0.06f);
}
```

