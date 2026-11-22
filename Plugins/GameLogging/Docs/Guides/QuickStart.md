# GameLogging 快速上手指南

本指南指导如何在自定义插件或游戏主模块中引入并使用 `GameLogging`。

---

## 步骤 1：在 Build.cs 中添加模块依赖

```csharp
public class MyModule : ModuleRules {
    public MyModule(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "GameCore",
            "GameLogging",
        });
    }
}
```

---

## 步骤 2：注册当前模块的专属分类

在模块主实现文件（例如 `Private/MyModule.cpp`）顶层引入宏头文件并注册：

```cpp
#include "MyModule.h"
#include "Logging/GameLogMacros.h"

GAME_REGISTER_MODULE_LOG(LogMyModule);

void FMyModule::StartupModule() {
    GAME_LOG(Log, TEXT("MyModule started successfully!"));
}

void FMyModule::ShutdownModule() {
    GAME_LOG(Log, TEXT("MyModule shutdown."));
}

IMPLEMENT_MODULE(FMyModule, MyModule)
```

> **提示**：仅使用日志宏时包含 `#include "Logging/GameLogMacros.h"` 最轻量；若同时需要访问 `LogGame` 兜底分类，可包含 `#include "Logging/GameLogCategories.h"`；若希望聚合包含全部日志与远程管道设施，可包含 `#include "GameLogging.h"`。

---

## 步骤 3：在业务代码中直接使用

```cpp
#include "Logging/GameLogMacros.h"

void USomeGameplaySubsystem::ProcessEvent(int32 EventId) {
    GAME_LOG(Log, TEXT("Processing Event ID: %d"), EventId);
    GAME_CLOG(EventId < 0, Error, TEXT("Invalid negative EventId: %d"), EventId);
    GAME_CLOG_ONCE(EventId == 999, Warning, TEXT("EventId 999 is reserved for testing!"));
}
```

---

## 步骤 4：在控制台中过滤与查看日志

可在控制台动态调整分类级别：

```text
Log LogMyModule Verbose
```

或在 `DefaultEngine.ini` 中持久配置：

```ini
[Core.Log]
LogMyModule=Verbose
```

---

## 步骤 5：监听与转发远程结构化日志（可选）

若需将关键告警/错误上报至运维服务，只需在具备 `GameMessage` 依赖的模块中订阅 `TAG_Game_Log_Remote`：

```cpp
#include "Message/GameplayMessageSubsystem.h"
#include "Remote/GameRemoteLogTypes.h"

void UTelemetryManager::Initialize() {
    UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
    LogListenerHandle = MessageSubsystem.RegisterListener<FGameRemoteLogEntry>(
        TAG_Game_Log_Remote,
        [this](const FGameRemoteLogEntry& Entry) {
            if (Entry.GetVerbosity() <= ELogVerbosity::Error) {
                SendErrorToRemoteServer(Entry.Category, Entry.Message, Entry.Timestamp, Entry.FrameNumber);
            }
        }
    );
}
```
