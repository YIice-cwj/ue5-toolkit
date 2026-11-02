# [三振出局阻断求助报告] GameCore Green 验证沙箱权限

> **触发铁律**：B8 三振出局，试错熔断
> **状态**：已解除。用户在沙箱外执行验证；首轮 Green 11/11、定向回归 27/27、完整 GameCore 回归 39/39 全部通过，最终退出码均为 0
> **日期**：2026-10-30

---

## 1. 阻断现象与环境上下文

- **触发任务**：执行 GameCore 对象池、Config、Observer、Time 新增用例的 Green 验证与定向回归。
- **阻断阶段**：UnrealEditor-Cmd 启动后自动调用 UBT，在 `ValidatePlatforms` 阶段退出；尚未进入任何目标自动化用例。
- **工程**：`/Users/yiice/code/UE5/UEToolkit/UEToolkit.uproject`
- **引擎**：UE 5.8，macOS arm64。
- **最新后台命令 ID**：`591909fd6748a9bef73199bfd3b05e80`，退出码 1。
- **核心报错信息**：

```text
System.UnauthorizedAccessException: Access to the path '/Users/yiice/Library/Application Support/Epic/UnrealBuildTool/Trace-backup-2026.10.30-07.18.09.uba' is denied.
 ---> System.IO.IOException: Permission denied
   --- End of inner exception stack trace ---
   at System.IO.FileSystem.DeleteFile(String fullPath)
   at EpicGames.Core.FileReference.Delete(FileReference location)
   at EpicGames.Core.Log.BackupLogFile(FileReference outputFile)
   at UnrealBuildTool.UnrealBuildTool.Main(String[] ArgumentsArray)
```

- **最新沙箱限制清单**：

```text
TRAE Sandbox Error: hit restricted
Not allow operate files:
/Users/yiice/Library/Application Support/Epic/UnrealBuildTool/Trace-backup-2026.10.30-07.18.09.uba
/Users/yiice/Library/Application Support/Epic/ZenServerInstall
/Users/yiice/.zen/History/0af5e19c89216bfbc2d6fef4
/Users/yiice/Library/Application Support/Epic/UnrealEngine/Common/DerivedDataCache/TestData/5C87AF08E144F80B599E31BC559D163B/TestData/0/TestData_4kb.dat
/Users/yiice/Library/Application Support/Epic/UnrealEngine/Common/DerivedDataCache/TestData/5C87AF08E144F80B599E31BC559D163B/TestData/0
/Users/yiice/Library/Application Support/Epic/UnrealEngine/Common/DerivedDataCache/TestData/5C87AF08E144F80B599E31BC559D163B/TestData
/Users/yiice/Library/Application Support/Epic/UnrealEngine/Common/DerivedDataCache/TestData/5C87AF08E144F80B599E31BC559D163B
/Users/yiice/Library/Application Support/Epic/UE_AnalyticsSessionSummaryManager_OrphanOwner
/Users/yiice/Library/Application Support/Epic/UnrealEngine/Common/Analytics/5389AAA0AE4AE8B15184F984B34609DF_19003_19003_0_Editor
/Users/yiice/Library/Application Support/Epic/UnrealEngine/5.8/Saved/Config/MacEditor/EditorSettings.ini
/Users/yiice/Library/Application Support/Epic/ProjectEditorRecords
/Users/yiice/Library/Application Support/Epic/Unreal.uproject
/Users/yiice/Library/Application Support/Epic/UnrealEngine/Editor/ProjectEditorRecords.json
/Users/yiice/Library/HTTPStorages/UnrealEditor-Cmd/httpstorages.sqlite
/Users/yiice/Library/HTTPStorages/UnrealEditor-Cmd/httpstorages.sqlite-wal
/Users/yiice/Library/HTTPStorages/UnrealEditor-Cmd/httpstorages.sqlite-shm
/Users/yiice/Library/Application Support/Epic/__________________yiice_code_UE5_UEToolkit_Intermediate_AssetRegistryCacheLock
/Users/yiice/Library/Application Support/Epic/UnrealEngine/Editor/ContentBrowser.json
/Users/yiice/Library/Application Support/Epic/UnrealEngine/Editor/FilterBar.json
/Users/yiice/Library/HTTPStorages/UnrealEditor-Cmd.binarycookies_tmp_19003.dat
```

---

## 2. 前 3 轮权限处置复盘与客观失效事实

| 轮次 | 尝试方案核心假设 | 具体处置 | 客观失败结果 |
|:---:|:---|:---|:---|
| Round 1 | 首次超时可能是个别启动问题，直接定向运行可取得用例结果 | 使用 UnrealEditor-Cmd 定向启动 GameCore 测试 | UBT 在写真实用户目录的 Trace 备份时被拒绝，未执行目标用例 |
| Round 2 | 执行路径的沙箱限制可能导致写入失败 | 重试直接引擎测试，并尝试非沙箱执行路径 | 仍返回相同 `UnauthorizedAccessException` 与 TRAE Sandbox 限制，未执行目标用例 |
| Round 3 | UBT/UE 可能遵循 `HOME`，临时 HOME 可隔离用户目录写入 | 设置 `HOME=/tmp/UEToolkit-UEHome` 后启动同一目标测试 | .NET/UE 仍写 `/Users/yiice/Library/...`，同一权限错误再次出现，退出码 1 |

另：首次 Pool Green 启动曾达到编辑器初始化阶段但 600 秒超时，日志没有任何 Pool 用例执行记录；该结果未被当作对象池失败或通过证据。

---

## 3. 根因假设反思与知识盲区

- 连续失败的共同点是：测试启动器隐式调用 UBT，且 .NET/UE 使用 macOS 真实用户目录，而不是仅依赖进程 `HOME` 环境变量。
- 当前阻断属于代理执行沙箱的文件系统权限边界，不是 GameCore 测试断言、C++ 编译诊断或通知调度逻辑暴露的失败。
- 已排除“只改测试筛选器”“重复直接启动”“仅设置临时 HOME”能够消除权限阻断的假设。
- 尚未验证用户主动调整沙箱规则或在沙箱外运行后的结果，不能据此推断 Green 用例会通过。

### 当前已有客观证据

1. 七个新增回归用例已在旧实现上按预期失败，Red 证据有效。
2. 新 `FNotificationDispatcher.cpp.o`、`FGlobalDilationMixer.cpp.o` 与 `libUnrealEditor-GameCore.dylib` 已生成；时间戳确认新实现进入过构建产物。
3. 最后两处修改为纯 Doxygen 文档同步；之后未再执行最终构建。
4. `git diff --check -- Plugins/GameCore/Source/GameCore` 通过。
5. Green、定向回归及全量回归均未取得完成结果，不得宣称测试通过。

---

## 4. 待决策方案选项

### 选项 A（推荐）：在本机沙箱外执行测试并回传日志

由人类工程师在终端运行：

```bash
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "/Users/yiice/code/UE5/UEToolkit/UEToolkit.uproject" \
  "-ExecCmds=Automation RunTests Game.Core.Pool+Game.Core.Config.NotificationCommitOrder+Game.Core.Observer+Game.Core.Time.DilationMixerCrossBoundary; Quit" \
  -unattended -nullrhi -nosplash \
  -DisablePlugins=GameGameplay,GameFeedback \
  -DDC-ForceMemoryCache -NoZenAutoLaunch -notraceserver \
  -log=GameCoreGreen.log
```

日志位置：`Saved/Logs/GameCoreGreen.log`。需提供各目标用例的 `Test Completed` 与最终 `TEST COMPLETE. EXIT CODE: 0`，之后再运行 Config、Observer、ObservableRegistry、ListenerHub、Time、Pool 定向回归。

### 选项 B：调整 Trae 自定义沙箱后授权恢复

允许测试进程写入上述 Epic、UBT、Zen、DDC、Analytics、EditorSettings 与 HTTPStorages 用户目录。该方案会扩大代理写权限，需人类明确批准；授权前不继续重试。

### 选项 C：改由可信 macOS CI/UE 主机执行

在具备 UE 5.8、相同工作区及所需用户目录写权限的环境执行同一测试命令，并提供完整日志与退出码。
