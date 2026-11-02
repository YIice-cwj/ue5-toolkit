# GameData Review Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Do not commit or push without separate explicit user authorization.

**Goal:** 根治 GameData 存档代次竞争、异步加载乱序、JSON/I/O 失败契约及审查发现的注释问题，并用确定性自动化测试证明修复有效。

**Architecture:** 保持现有模块边界：`FSaveGameFileIO` 独占保存代次和文件写入，`USaveGameSubsystem` 在主线程独占加载代次与业务段应用。公开函数签名、Blueprint 调用方式、存档格式和空摘要运行时行为均不变。

**Tech Stack:** UE 5.8、C++20、Unreal Automation Test、TaskGraph、现有 `run_automation_tests.py`。

---

## 文件边界

- 修改 `Plugins/GameData/Source/GameDataCore/Private/Save/FSaveGameFileIO.cpp`：保存代次、高水位保留、删除与恢复结果。
- 修改 `Plugins/GameData/Source/GameDataCore/Public/Save/USaveGameSubsystem.h`：加载代次成员及公开契约注释。
- 修改 `Plugins/GameData/Source/GameDataCore/Private/Save/USaveGameSubsystem.cpp`：最新异步加载优先与删除失效。
- 修改 `Plugins/GameData/Source/GameDataCore/Public/Utils/DataTableLoaderUtils.h`：统一 JSON 失败输出。
- 修改 `Plugins/GameData/Source/GameDataCore/Public/StaticData/FStaticDataRepository.h`、`FStaticDataBinCodec.h`：文档头同步。
- 修改 `Plugins/GameData/Source/GameDataCore/Private/Tests/SaveGameSubsystemTest.cpp`：保存 ABA、异步最新请求优先及测试文档头。
- 修改 `Plugins/GameData/Source/GameDataCore/Private/Tests/FStaticDataRepositoryTest.cpp`：JSON 契约测试及测试文档头。
- 修改 `Plugins/GameData/Source/GameDataCore/Private/Tests/FStaticDataBinCodecTest.cpp`：空摘要契约及测试文档头。
- 修改 `Plugins/GameData/Docs/API/SaveGameSubsystem.md`、`StaticDataRepository.md` 和 `Docs/Architecture/PersistenceArchitecture.md`：公开行为同步。

构建命令统一为：

```bash
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" UEToolkitEditor Mac Development -Project="/Users/yiice/code/UE5/UEToolkit/UEToolkit.uproject" -WaitMutex
```

预期：退出码 0，无 C++/UHT 编译错误。

### Task 1：保存 ABA 回归与代次修复

**Files:**
- Test: `Plugins/GameData/Source/GameDataCore/Private/Tests/SaveGameSubsystemTest.cpp`
- Modify: `Plugins/GameData/Source/GameDataCore/Private/Save/FSaveGameFileIO.cpp`

- [ ] **Step 1：写入确定性失败测试**

在 `SaveGameSubsystemTest.cpp` 增加 `Game.Data.SaveGeneration` 用例。使用真实 `USaveGame` 序列化字节和文件 I/O，显式排列代次，不创建线程：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameGenerationTest,
    "Game.Data.SaveGeneration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FSaveGameGenerationTest::RunTest(const FString& Parameters) {
    const FString SlotName = TEXT("UnitTest_SaveGeneration");
    FSaveGameFileIO::DeleteSaveSlot(SlotName);

    UGameSaveTestArchive* OldArchive = NewObject<UGameSaveTestArchive>();
    UGameSaveTestArchive* NewArchive = NewObject<UGameSaveTestArchive>();
    OldArchive->Value = 11;
    NewArchive->Value = 99;

    TArray<uint8> OldBytes;
    TArray<uint8> NewBytes;
    TestTrue(TEXT("旧档内存序列化成功"), FSaveGameFileIO::SerializeObject(OldArchive, OldBytes));
    TestTrue(TEXT("新档内存序列化成功"), FSaveGameFileIO::SerializeObject(NewArchive, NewBytes));

    const uint64 OldGeneration = FSaveGameFileIO::IssueSlotSaveGeneration(SlotName);
    FSaveGameFileIO::DeleteSaveSlot(SlotName);
    const uint64 NewGeneration = FSaveGameFileIO::IssueSlotSaveGeneration(SlotName);
    TestTrue(TEXT("删除后的新代次严格大于旧代次"), NewGeneration > OldGeneration);

    TestTrue(TEXT("新档写入成功"), FSaveGameFileIO::WriteSlotBytes(SlotName, NewBytes, NewGeneration));
    TestTrue(TEXT("过期写被安全吸收"), FSaveGameFileIO::WriteSlotBytes(SlotName, OldBytes, OldGeneration));

    TArray<uint8> LoadedBytes;
    TestTrue(TEXT("最终存档读取成功"), FSaveGameFileIO::ReadSlotBytes(SlotName, LoadedBytes));
    TestTrue(TEXT("过期写没有覆盖新档"), LoadedBytes == NewBytes);

    FSaveGameFileIO::DeleteSaveSlot(SlotName);
    return true;
}
```

为该类的 `RunTest` 添加现有多行 Doxygen 文档头，`@brief` 描述删除后旧写不得复活或覆盖，`@param Parameters` 和 `@return` 与既有测试一致。

- [ ] **Step 2：编译并确认测试在旧实现失败**

先运行统一构建命令，再运行：

```bash
python3 .scripts/run_automation_tests.py --case Game.Data.SaveGeneration --timeout 10
```

预期：`删除后的新代次严格大于旧代次` 和 `过期写没有覆盖新档` 至少一项失败；不得把该预期红灯记录为交付通过。

- [ ] **Step 3：保留已签发高水位**

在 `InvalidateSlotSaveGeneration` 中删除以下语句，其他逻辑不变：

```cpp
GetIssuedSlotGenerations().Remove(SlotName);
```

将其文档头更新为：

```cpp
/**
 * @brief 使槽位全部在途写失效（写入删除墓碑并保留已签发高水位）
 * @param SlotName 目标槽位名称
 */
```

`IssueSlotSaveGeneration` 文档头补充“删除后仍沿已签发高水位继续递增”。这样删除墓碑被下一次签发清除时，旧代次仍严格小于新代次。

- [ ] **Step 4：验证保存修复**

重新构建并运行 Step 2 命令。

预期：用例通过；再运行 `python3 .scripts/run_automation_tests.py --case Game.Data.SaveSubsystemGeneric --timeout 10`，既有同步保存、加载、未知段保留与恢复测试全部通过。

### Task 2：异步加载最新请求优先

**Files:**
- Test: `Plugins/GameData/Source/GameDataCore/Private/Tests/SaveGameSubsystemTest.cpp`
- Modify: `Plugins/GameData/Source/GameDataCore/Public/Save/USaveGameSubsystem.h`
- Modify: `Plugins/GameData/Source/GameDataCore/Private/Save/USaveGameSubsystem.cpp`
- Docs: `Plugins/GameData/Docs/API/SaveGameSubsystem.md`
- Docs: `Plugins/GameData/Docs/Architecture/PersistenceArchitecture.md`

- [ ] **Step 1：加入真实回调驱动的失败测试**

在匿名命名空间增加共享状态。所有异步委托仅捕获该共享状态，禁止捕获 `RunTest` 局部变量的引用：

```cpp
/**
 * @brief 最新异步加载测试的跨续体共享状态
 */
struct FAsyncLoadLatestTestState {
    TWeakObjectPtr<USaveGameSubsystem> Subsystem;
    FString SlotName;
    int32 SourceValue = 77;
    int32 LoadedValue = 0;
    int32 ApplyCount = 0;
    TArray<bool> CompletionResults;
};
```

增加 latent command；只有收到两个真实完成回调后才断言并清理：

```cpp
/**
 * @brief 等待最新异步加载测试的全部完成回调并验证结果
 */
class FWaitForAsyncLoadLatestCommand : public IAutomationLatentCommand {
public:
    /**
     * @brief 构造等待命令
     * @param InTest 当前自动化测试上下文
     * @param InState 跨后台任务与主线程续体共享的状态
     */
    FWaitForAsyncLoadLatestCommand(FAutomationTestBase& InTest, const TSharedRef<FAsyncLoadLatestTestState>& InState)
        : Test(InTest)
        , State(InState) {
    }

    /**
     * @brief 在回调未收齐时继续等待，收齐后验证并结束命令
     * @return 两个完成回调均已到达并执行断言返回 true，否则返回 false
     */
    bool Update() override {
        if (State->CompletionResults.Num() < 2) {
            return false;
        }

        Test.TestEqual(TEXT("只有最新请求应用业务段"), State->ApplyCount, 1);
        Test.TestEqual(TEXT("最新请求应用了存档值"), State->LoadedValue, State->SourceValue);
        Test.TestTrue(TEXT("过期请求以取消结束"), State->CompletionResults.Contains(false));
        Test.TestTrue(TEXT("最新请求成功完成"), State->CompletionResults.Contains(true));
        if (State->Subsystem.IsValid()) {
            State->Subsystem->DeleteSaveSlot(State->SlotName);
        }
        return true;
    }

private:
    FAutomationTestBase& Test;
    TSharedRef<FAsyncLoadLatestTestState> State;
};
```

新增 `Game.Data.SaveSubsystemAsyncLatest`：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSaveGameSubsystemAsyncLatestTest,
    "Game.Data.SaveSubsystemAsyncLatest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FSaveGameSubsystemAsyncLatestTest::RunTest(const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    USaveGameSubsystem* Subsystem = CreateTestSubsystem(GameInstance);
    if (Subsystem == nullptr) {
        return false;
    }

    const FString SlotName = TEXT("UnitTest_AsyncLatest");
    const FString SegmentName = TEXT("AsyncLatest");
    Subsystem->SetAllowedSlotNames({ SlotName });

    TSharedRef<FAsyncLoadLatestTestState> State = MakeShared<FAsyncLoadLatestTestState>();
    State->Subsystem = Subsystem;
    State->SlotName = SlotName;

    FGameSaveSegment Segment;
    Segment.SegmentName = FName(*SegmentName);
    Segment.SegmentVersion = 1;
    Segment.Save = [State](FArchive& Ar) {
        Ar << State->SourceValue;
    };
    Segment.Load = [State](FArchive& Ar, int32 StoredVersion) {
        if (StoredVersion != 1) {
            return false;
        }
        Ar << State->LoadedValue;
        ++State->ApplyCount;
        return !Ar.IsError();
    };
    Subsystem->RegisterSegment(Segment);
    Subsystem->OnLoadComplete.AddLambda([State](const FString&, bool bSuccess) {
        State->CompletionResults.Add(bSuccess);
    });

    TestTrue(TEXT("准备异步加载存档成功"), Subsystem->SaveGameToSlot(SlotName));
    State->LoadedValue = 0;
    State->ApplyCount = 0;

    TestTrue(TEXT("第一个异步加载派发成功"), Subsystem->LoadGameFromSlotAsync(SlotName));
    TestTrue(TEXT("第二个异步加载派发成功"), Subsystem->LoadGameFromSlotAsync(SlotName));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForAsyncLoadLatestCommand(*this, State));
    return true;
}
```

补齐该 `RunTest` 的 Doxygen 文档头。测试不使用 sleep，也不访问子系统私有代次。

- [ ] **Step 2：确认旧实现不能通过**

构建并运行：

```bash
python3 .scripts/run_automation_tests.py --case Game.Data.SaveSubsystemAsyncLatest --timeout 10
```

预期：旧实现两次业务段均应用，`ApplyCount == 1` 断言失败，且没有取消回调。

- [ ] **Step 3：在主线程签发和核验加载代次**

在 `USaveGameSubsystem.h` 私有成员末尾增加：

```cpp
TMap<FString, uint64> AsyncLoadGenerations;
```

在 `Deinitialize` 中、调用 `Super::Deinitialize()` 前增加：

```cpp
AsyncLoadGenerations.Empty();
```

在 `LoadGameFromSlotAsync` 通过槽位校验后、`WeakThis` 创建前增加：

```cpp
const uint64 LoadGeneration = ++AsyncLoadGenerations.FindOrAdd(SlotName);
```

后台读取 lambda 与主线程续体均按值捕获 `LoadGeneration`。将主线程续体替换为以下逻辑：

```cpp
FGameThreadTaskRunner::RunOnGameThread(WeakThis, [WeakThis, SlotName, LoadGeneration, bReadSuccess, Payload = MoveTemp(Bytes)]() mutable {
    const bool bIsLatest = WeakThis->AsyncLoadGenerations.FindChecked(SlotName) == LoadGeneration;
    bool bSuccess = false;
    if (bIsLatest && bReadSuccess) {
        USaveGame* LoadedObject = UGameplayStatics::LoadGameFromMemory(Payload);
        if (LoadedObject != nullptr && LoadedObject->IsA(WeakThis->SaveGameClass)) {
            UGameSaveArchive* Archive = CastChecked<UGameSaveArchive>(LoadedObject);
            bSuccess = Archive->Metadata.ArchiveVersion <= UGameSaveArchive::CurrentArchiveVersion
                && WeakThis->SegmentRegistry.ApplyArchive(SlotName, *Archive);
        }
    }

    if (!bIsLatest) {
        GAME_LOG(Log, TEXT("【存档子系统】过期异步加载已取消: %s"), *SlotName);
    } else if (bSuccess) {
        GAME_LOG(Log, TEXT("【存档子系统】异步成功装载槽位: %s"), *SlotName);
    } else {
        GAME_LOG(Error, TEXT("【存档子系统】异步装载槽位失败: %s"), *SlotName);
    }
    WeakThis->OnLoadComplete.Broadcast(SlotName, bSuccess);
});
```

在 `DeleteSaveSlot` 通过槽位校验后、清理保留段前增加：

```cpp
++AsyncLoadGenerations.FindOrAdd(SlotName);
```

- [ ] **Step 4：同步加载与删除公开契约**

`LoadGameFromSlotAsync` 文档头改为：

```cpp
/**
 * @brief 异步从指定槽位读取分段归档（后台读文件，主线程仅应用最新请求）
 * @param SlotName 目标存档槽位名称
 * @return 成功派发异步任务返回 true，被白名单拦截返回 false
 *
 * 同槽位重复调用时仅最后派发的请求可应用；过期请求不调用业务 Load，完成事件以 false 表示取消。
 */
```

`DeleteSaveSlot` 文档头补充“同时使该槽位在途异步加载失效”；`Deinitialize` 文档头补充清空加载代次。API 与架构文档同步相同语义，不再把取消描述为磁盘损坏。

- [ ] **Step 5：验证异步修复**

构建并运行 Step 2 命令及 `Game.Data.SaveSubsystemGeneric`。

预期：异步用例只应用一次，回调严格包含一次 `false` 和一次 `true`；同步测试全部通过。

### Task 3：JSON 失败输出契约

**Files:**
- Test: `Plugins/GameData/Source/GameDataCore/Private/Tests/FStaticDataRepositoryTest.cpp`
- Modify: `Plugins/GameData/Source/GameDataCore/Public/Utils/DataTableLoaderUtils.h`

- [ ] **Step 1：写入前置失败与成功路径测试**

在测试文件增加：

```cpp
#include "Misc/FileHelper.h"
#include "Utils/DataTableLoaderUtils.h"
```

新增 `Game.Data.DataTableLoaderUtils` 用例，以独立 Saved 测试目录创建真实 JSON：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDataTableLoaderUtilsTest,
    "Game.Data.DataTableLoaderUtils",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FDataTableLoaderUtilsTest::RunTest(const FString& Parameters) {
    const FString TestDirectory = FPaths::ProjectSavedDir() / TEXT("UnitTest_DataTableLoaderUtils");
    IFileManager& FileManager = IFileManager::Get();
    FileManager.DeleteDirectory(*TestDirectory, false, true);
    TestTrue(TEXT("创建 JSON 测试目录成功"), FileManager.MakeDirectory(*TestDirectory, true));

    TMap<FName, FRepositoryTestRowA> OutRows;
    FRepositoryTestRowA OldRow;
    OldRow.Id = 7;
    OldRow.Name = TEXT("Old");
    OutRows.Add(FName(TEXT("Old")), OldRow);

    const FString MissingPath = TestDirectory / TEXT("Missing.json");
    TestFalse(TEXT("缺失文件加载失败"), FDataTableLoaderUtils::LoadTableFromJson(MissingPath, OutRows));
    TestEqual(TEXT("缺失文件清空预填充输出"), OutRows.Num(), 0);

    OldRow.Id = 8;
    OutRows.Add(FName(TEXT("OldAgain")), OldRow);
    const FString MalformedPath = TestDirectory / TEXT("Malformed.json");
    TestTrue(TEXT("写入损坏 JSON 成功"), FFileHelper::SaveStringToFile(TEXT("{ not valid json"), *MalformedPath));
    TestFalse(TEXT("损坏 JSON 加载失败"), FDataTableLoaderUtils::LoadTableFromJson(MalformedPath, OutRows));
    TestEqual(TEXT("损坏 JSON 清空预填充输出"), OutRows.Num(), 0);

    const FString ValidPath = TestDirectory / TEXT("Valid.json");
    const FString ValidJson = TEXT("{\"rows\":[{\"name\":\"Alpha\",\"row\":{\"id\":42,\"name\":\"Loaded\"}}]}");
    TestTrue(TEXT("写入有效 JSON 成功"), FFileHelper::SaveStringToFile(ValidJson, *ValidPath));
    TestTrue(TEXT("有效 JSON 加载成功"), FDataTableLoaderUtils::LoadTableFromJson(ValidPath, OutRows));
    TestEqual(TEXT("有效 JSON 行数"), OutRows.Num(), 1);
    TestEqual(TEXT("有效 JSON 行键"), OutRows.Find(FName(TEXT("Alpha")))->Id, 42);

    FileManager.DeleteDirectory(*TestDirectory, false, true);
    return true;
}
```

为 `RunTest` 补齐规范 Doxygen 文档头。读取失败与缺失文件共用入口清空逻辑，不引入生产 I/O 注入接口仅为测试设卡。

- [ ] **Step 2：确认旧实现失败**

构建并运行：

```bash
python3 .scripts/run_automation_tests.py --case Game.Data.DataTableLoaderUtils --timeout 10
```

预期：缺失文件和损坏 JSON 的“清空预填充输出”断言失败，有效 JSON 路径成功。

- [ ] **Step 3：入口统一清空**

把 `OutMap.Empty()` 移到函数体的第一条语句，删除 JSON 解析成功后原来的重复清空；行内失败时的清空保留，因为此时可能已写入部分行：

```cpp
static bool LoadTableFromJson(const FString& JsonPath, TMap<FName, TRowType>& OutMap) {
    OutMap.Empty();

    if (!FPaths::FileExists(JsonPath)) {
        GAME_LOG(Warning, TEXT("【JSON加载】文件不存在: %s"), *JsonPath);
        return false;
    }
```

其余实现不变。

- [ ] **Step 4：验证 JSON 契约**

构建并运行 Step 2 命令。

预期：全部断言通过，测试目录已清理。

### Task 4：文件删除与临时档恢复结果

**Files:**
- Modify: `Plugins/GameData/Source/GameDataCore/Private/Save/FSaveGameFileIO.cpp`

- [ ] **Step 1：收紧删除整体成功语义**

将 `DeleteSaveSlot` 的状态变量改为：

```cpp
bool bFoundAnyFile = false;
bool bDeleteFailed = false;
```

正式档与临时档两个存在分支均执行实际删除，并分别累计：

```cpp
bFoundAnyFile = true;
bDeleteFailed |= !IFileManager::Get().Delete(*FinalPath, false, true);
```

临时档使用 `TempPath`。返回值与日志条件改为：

```cpp
const bool bDeleted = bFoundAnyFile && !bDeleteFailed;
```

保持无文件返回 `false`，保留互斥锁和前后日志结构。

- [ ] **Step 2：检查恢复换入结果**

将 `ReadSlotBytes` 中恢复分支改为：

```cpp
if (!IFileManager::Get().Move(*FinalPath, *TempPath, true, true, true, false)) {
    GAME_LOG(Error, TEXT("【存档IO】临时档恢复换入失败: %s"), *FinalPath);
    return false;
}
return true;
```

同步 `ReadSlotBytes` 文档头，明确临时档字节有效但换入失败也返回 `false`。

- [ ] **Step 3：验证既有正常恢复路径**

构建并运行：

```bash
python3 .scripts/run_automation_tests.py --case Game.Data.SaveSubsystemGeneric --timeout 10
```

预期：正常临时档恢复、正式档恢复、未知段保留等既有断言全部通过。文件系统拒绝删除/换入属于外部故障分支，本次不新增生产故障注入抽象。

### Task 5：空摘要契约与全部文档头

**Files:**
- Test: `Plugins/GameData/Source/GameDataCore/Private/Tests/FStaticDataBinCodecTest.cpp`
- Test: `Plugins/GameData/Source/GameDataCore/Private/Tests/FStaticDataRepositoryTest.cpp`
- Test: `Plugins/GameData/Source/GameDataCore/Private/Tests/SaveGameSubsystemTest.cpp`
- Modify: `Plugins/GameData/Source/GameDataCore/Public/StaticData/FStaticDataRepository.h`
- Modify: `Plugins/GameData/Source/GameDataCore/Public/StaticData/FStaticDataBinCodec.h`
- Modify: `Plugins/GameData/Source/GameDataCore/Public/Save/USaveGameSubsystem.h`
- Docs: `Plugins/GameData/Docs/API/StaticDataRepository.md`

- [ ] **Step 1：锁定空输入 SHA-1 行为**

在 `FStaticDataBinCodecTest::RunTest` 的空表集合往返测试后增加：

```cpp
const FStaticDataContentDigest EmptyDigest = FStaticDataBinCodec::ComputeDigest(EmptyTables);
TestEqual(TEXT("空集合摘要表数为零"), EmptyDigest.TableCount, 0);
TestEqual(TEXT("空集合摘要行数为零"), EmptyDigest.TotalRowCount, 0);
TestEqual(
    TEXT("空集合摘要为 SHA1 空输入值"),
    EmptyDigest.Sha1Hash,
    FString(TEXT("da39a3ee5e6b4b0d3255bfef95601890afd80709"))
);
```

- [ ] **Step 2：修正生产函数文档头**

将空摘要 `@return` 统一改为：

```cpp
 * @return 表数、总行数与 SHA1 摘要（空仓库为 SHA1 空输入摘要）
```

`FStaticDataBinCodec::ParseTables` 的读取器参数改为：

```cpp
 * @param PayloadReader 已消费 Magic 与版本、定位到表数量字段的读取器
```

`USaveGameSubsystem::SaveGameToSlotAsync` 文档头首行改为：

```cpp
 * @brief 异步保存全部已注册段（主线程捕获并序列化，后台线程写文件，完成后回主线程广播）
```

`Docs/API/StaticDataRepository.md` 同步空摘要说明，不修改 `ComputeDigest` 实现。

- [ ] **Step 3：补齐既有测试辅助函数文档头**

为 `FStaticDataRepositoryTest.cpp` 的 `HandleRowAField`、`HandleRowBField`、`HandleRowA`、`HandleRowB`、`MakeRowA`、`RegisterRowA`、`RegisterRowB`，`FStaticDataBinCodecTest.cpp` 的 `RunTest`，以及 `SaveGameSubsystemTest.cpp` 的 `CreateTestSubsystem`、`RegisterTestSegment` 分别添加多行 Doxygen 块。

每块必须直接描述现有实现：`Handle*` 标明归档和行参数、返回 `!Ar.IsError()`；`MakeRowA` 标明输入 Id 与返回确定性测试行；`Register*` 标明仓库参数且无返回值标签；`CreateTestSubsystem` 标明输出 GameInstance 与可能为空的子系统返回；`RegisterTestSegment` 标明子系统、段名和引用捕获值的参数。禁止单行压缩，禁止增加实现中不存在的防御或行为说明。

- [ ] **Step 4：验证空摘要与注释**

构建并运行：

```bash
python3 .scripts/run_automation_tests.py --case Game.Data.StaticDataBinCodec --timeout 10
```

预期：空集合 SHA-1 与既有 Magic、CRC、重复键、多表编码测试全部通过。逐个人工核对修改函数的文档头与实现，无业务行内注释。

### Task 6：全量验证与差异审计

**Files:**
- Review only: 本计划列出的全部文件

- [ ] **Step 1：运行 Game.Data 全量测试**

先运行统一构建命令，再运行：

```bash
python3 .scripts/run_automation_tests.py --case Game.Data --timeout 10
```

预期：构建退出码 0；所有 `Game.Data` 用例 `Result={Success}`，日志出现 `TEST COMPLETE. EXIT CODE: 0`。若外层脚本在引擎退出后因沙箱拦截 Epic 用户目录写入而返回非零，必须同时引用引擎成功标记与沙箱诊断，不得混报为测试失败。

- [ ] **Step 2：执行最终静态检查**

运行：

```bash
git diff --check -- Plugins/GameData Docs/Plans/2026-10-30-GameData-review-fix.md Docs/Plans/2026-10-30-GameData-review-fix-implementation.md
```

预期：无空白错误。搜索本次测试文件中的 `Sleep`、`wait`，确认没有 sleep 盲等；核对所有新函数和修改函数均有同步的多行 Doxygen 文档头。

- [ ] **Step 3：审计变更边界**

使用 `git status --short` 和针对上述文件的 `git diff`，确认本次只修改计划内代码、测试和文档；工作区其他既有修改保持原样。不得执行 `git commit`、`git push`、`git reset`、`git checkout` 或清理命令。

- [ ] **Step 4：交付验证摘要**

报告每项缺陷的根因、修改文件、失败到通过的测试证据、全量测试数量及未直接注入的 OS 文件故障分支。不得仅以主观阅读宣称完成。
