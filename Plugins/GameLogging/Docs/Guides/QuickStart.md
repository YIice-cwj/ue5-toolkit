# GameLogging 快速上手指南

本指南指导如何在您的自定义插件或游戏主模块中引入并使用 `GameLogging`。

---

## 步骤 1：在 Build.cs 中添加模块依赖

在目标模块的 `MyModule.Build.cs` 中添加依赖：

```csharp
public class MyModule : ModuleRules
{
    public MyModule(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "GameCore",
            "GameLogging", // 添加 GameLogging 依赖
        });
    }
}
```

---

## 步骤 2：注册当前模块的专属分类

打开模块的主实现文件（例如 `Private/MyModule.cpp`），在文件顶层写入：

```cpp
#include "MyModule.h"
#include "Logging/GameLogCategories.h"

// 一行代码声明并注册当前模块的专属分类 LogMyModule
GAME_REGISTER_MODULE_LOG(LogMyModule);

void FMyModule::StartupModule()
{
    GAME_LOG(Log, TEXT("MyModule started successfully!"));
}

void FMyModule::ShutdownModule()
{
    GAME_LOG(Log, TEXT("MyModule shutdown."));
}

IMPLEMENT_MODULE(FMyModule, MyModule)
```

---

## 步骤 3：在业务代码中直接使用

在任意头文件或源文件中，包含宏定义头文件后即可直接使用：

```cpp
#include "Logging/GameLogCategories.h"

void USomeGameplaySubsystem::ProcessEvent(int32 EventId)
{
    // 普通输出
    GAME_LOG(Log, TEXT("Processing Event ID: %d"), EventId);

    // 条件告警
    GAME_CLOG(EventId < 0, Error, TEXT("Invalid negative EventId: %d"), EventId);

    // 仅在首次出错时提醒
    GAME_CLOG_ONCE(EventId == 999, Warning, TEXT("EventId 999 is reserved for testing!"));
}
```

---

## 步骤 4：在控制台中过滤与查看日志

在 Unreal Editor 输出日志窗口（Output Log）或终端中，日志将带有您注册的分类标签：

```text
LogMyModule: Log: MyModule started successfully!
LogMyModule: Log: Processing Event ID: 42
LogMyModule: Error: Invalid negative EventId: -1
```

您可以在命令行或 Console 中按分类调整冗余级别：
```text
Log LogMyModule Verbose
```
或者在工程 `DefaultEngine.ini` 中永久配置：
```ini
[Core.Log]
LogMyModule=Verbose
```

