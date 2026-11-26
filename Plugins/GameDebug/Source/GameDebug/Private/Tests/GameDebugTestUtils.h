/**
 * @file GameDebugTestUtils.h
 * @brief GameDebug 自动化测试公共辅助工具（线程安全日志捕获、屏幕打印捕获与 UE 公共查询、LineBatcher 计数）
 */

#pragma once

#include "CoreMinimal.h"

#if WITH_AUTOMATION_TESTS

#include "Components/LineBatchComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/CriticalSection.h"
#include "Misc/CString.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"

namespace Game::Debug::Private {

using FPrintObserverFn = void (*)(uint64 Key, const FString& Message, const FColor& Color, float Duration, void* UserData);
void SetPrintTestObserver(FPrintObserverFn Observer, void* UserData) noexcept;

}  // namespace Game::Debug::Private

namespace Game::Debug::Test {

/**
 * @brief 屏幕打印捕获记录条目
 */
struct FCapturedPrintRecord {
    uint64 Key = MAX_uint64;
    FString Message;
    FColor Color = FColor::White;
    float Duration = 0.0f;
};

/**
 * @brief RAII 屏幕打印捕获守卫（捕获真实提交至 GEngine->AddOnScreenDebugMessage 的全部键、文本、颜色与钳制后时长）
 */
class FScopedPrintCapture final {
public:
    /**
     * @brief 开启屏幕消息、清空现有队列并挂载捕获钩子
     */
    FScopedPrintCapture()
        : bPrevEnabled(GEngine != nullptr ? GEngine->bEnableOnScreenDebugMessages : false) {
        if (GEngine != nullptr) {
            GEngine->bEnableOnScreenDebugMessages = true;
            GEngine->ClearOnScreenDebugMessages();
        }
        Game::Debug::Private::SetPrintTestObserver(&FScopedPrintCapture::OnPrintDispatched, this);
    }

    /**
     * @brief 卸载捕获钩子、清空测试期屏幕消息并还原原有开关
     */
    ~FScopedPrintCapture() {
        Game::Debug::Private::SetPrintTestObserver(nullptr, nullptr);
        if (GEngine != nullptr) {
            GEngine->ClearOnScreenDebugMessages();
            GEngine->bEnableOnScreenDebugMessages = bPrevEnabled;
        }
    }

    FScopedPrintCapture(const FScopedPrintCapture&) = delete;
    FScopedPrintCapture& operator=(const FScopedPrintCapture&) = delete;

    /**
     * @brief 清空已捕获记录及引擎屏幕消息队列
     */
    void Reset() {
        {
            FScopeLock ScopeLock(&RecordsLock);
            Records.Reset();
        }
        if (GEngine != nullptr) {
            GEngine->ClearOnScreenDebugMessages();
        }
    }

    /**
     * @brief 获取已捕获记录总数
     * @return 记录数
     */
    [[nodiscard]] int32 Num() const {
        FScopeLock ScopeLock(&RecordsLock);
        return Records.Num();
    }

    /**
     * @brief 获取全部捕获记录的线程安全副本快照
     * @return 捕获记录数组副本
     */
    [[nodiscard]] TArray<FCapturedPrintRecord> GetRecordsSnapshot() const {
        FScopeLock ScopeLock(&RecordsLock);
        return Records;
    }

    /**
     * @brief 按槽位键逆序查找最近一次写入该键的记录副本
     * @param Key 64 位槽位键
     * @param OutRecord 传出匹配记录
     * @return 找到返回 true
     */
    [[nodiscard]] bool FindLastByKey(uint64 Key, FCapturedPrintRecord& OutRecord) const {
        FScopeLock ScopeLock(&RecordsLock);
        for (int32 Index = Records.Num() - 1; Index >= 0; --Index) {
            if (Records[Index].Key == Key) {
                OutRecord = Records[Index];
                return true;
            }
        }
        return false;
    }

    /**
     * @brief 统计指定槽位键的捕获次数
     * @param Key 64 位槽位键
     * @return 匹配记录数
     */
    [[nodiscard]] int32 CountByKey(uint64 Key) const {
        FScopeLock ScopeLock(&RecordsLock);
        int32 Count = 0;
        for (const FCapturedPrintRecord& Record : Records) {
            if (Record.Key == Key) {
                ++Count;
            }
        }
        return Count;
    }

private:
    static void OnPrintDispatched(uint64 Key, const FString& Message, const FColor& Color, float Duration, void* UserData) {
        if (FScopedPrintCapture* Self = static_cast<FScopedPrintCapture*>(UserData)) {
            FScopeLock ScopeLock(&Self->RecordsLock);
            Self->Records.Add(FCapturedPrintRecord{Key, Message, Color, Duration});
        }
    }

    bool bPrevEnabled = false;
    mutable FCriticalSection RecordsLock;
    TArray<FCapturedPrintRecord> Records;
};

/**
 * @brief 线程安全测试日志捕获器，用于精确断言 LogGameDebug 分类、级别、次数与正文
 */
class FScopedDebugLogCapture final : public FOutputDevice {
public:
    /**
     * @brief 构造并挂载日志捕获器
     * @param InMessageFilter 需要匹配的子串过滤词（空串表示匹配全部日志）
     */
    explicit FScopedDebugLogCapture(const TCHAR* InMessageFilter)
        : MessageFilter(InMessageFilter != nullptr ? InMessageFilter : TEXT("")) {
        FOutputDeviceRedirector::Get()->AddOutputDevice(this);
        Reset();
    }

    /**
     * @brief 析构并卸载日志捕获器
     */
    ~FScopedDebugLogCapture() override {
        FOutputDeviceRedirector::Get()->RemoveOutputDevice(this);
    }

    FScopedDebugLogCapture(const FScopedDebugLogCapture&) = delete;
    FScopedDebugLogCapture& operator=(const FScopedDebugLogCapture&) = delete;
    FScopedDebugLogCapture(FScopedDebugLogCapture&&) = delete;
    FScopedDebugLogCapture& operator=(FScopedDebugLogCapture&&) = delete;

    /**
     * @brief 接收引擎输出日志并按过滤词记录匹配条目
     * @param Text 日志正文
     * @param Verbosity 日志级别
     * @param Category 日志分类
     */
    void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override {
        if (Text == nullptr) {
            return;
        }
        if (!MessageFilter.IsEmpty() && FCString::Strstr(Text, *MessageFilter) == nullptr) {
            return;
        }
        FScopeLock ScopeLock(&Lock);
        ++TotalCount;
        LastCategory = Category;
        LastVerbosity = Verbosity;
        LastMessage = Text;
    }

    /**
     * @brief 声明支持多线程并发日志写入
     * @return 恒返回 true
     */
    [[nodiscard]] bool CanBeUsedOnMultipleThreads() const override {
        return true;
    }

    /**
     * @brief 重置捕获计数与末次状态
     */
    void Reset() {
        FScopeLock ScopeLock(&Lock);
        TotalCount = 0;
        LastCategory = NAME_None;
        LastVerbosity = ELogVerbosity::NoLogging;
        LastMessage.Empty();
    }

    /**
     * @brief 获取匹配日志总次数
     * @return 捕获次数
     */
    [[nodiscard]] int32 GetTotalCount() const {
        FScopeLock ScopeLock(&Lock);
        return TotalCount;
    }

    /**
     * @brief 获取末次匹配日志的分类名称
     * @return 日志分类名
     */
    [[nodiscard]] FName GetLastCategory() const {
        FScopeLock ScopeLock(&Lock);
        return LastCategory;
    }

    /**
     * @brief 获取末次匹配日志的详细级别
     * @return 日志级别
     */
    [[nodiscard]] ELogVerbosity::Type GetLastVerbosity() const {
        FScopeLock ScopeLock(&Lock);
        return LastVerbosity;
    }

    /**
     * @brief 获取末次匹配日志正文
     * @return 日志正文
     */
    [[nodiscard]] FString GetLastMessage() const {
        FScopeLock ScopeLock(&Lock);
        return LastMessage;
    }

private:
    FString MessageFilter;
    mutable FCriticalSection Lock;
    int32 TotalCount = 0;
    FName LastCategory = NAME_None;
    ELogVerbosity::Type LastVerbosity = ELogVerbosity::NoLogging;
    FString LastMessage;
};

/**
 * @brief RAII 屏幕消息环境守卫：测试期启用并清空屏幕消息队列，析构自动恢复原开关并清空残留
 */
class FScopedScreenMessageGuard final {
public:
    /**
     * @brief 保存原有屏幕消息开关、开启屏幕消息并清空现有条目
     */
    FScopedScreenMessageGuard()
        : bPrevEnabled(GEngine != nullptr ? GEngine->bEnableOnScreenDebugMessages : false) {
        if (GEngine != nullptr) {
            GEngine->bEnableOnScreenDebugMessages = true;
            GEngine->ClearOnScreenDebugMessages();
        }
    }

    /**
     * @brief 清空测试期屏幕消息并还原原有开关状态
     */
    ~FScopedScreenMessageGuard() {
        if (GEngine != nullptr) {
            GEngine->ClearOnScreenDebugMessages();
            GEngine->bEnableOnScreenDebugMessages = bPrevEnabled;
        }
    }

    FScopedScreenMessageGuard(const FScopedScreenMessageGuard&) = delete;
    FScopedScreenMessageGuard& operator=(const FScopedScreenMessageGuard&) = delete;

private:
    bool bPrevEnabled = false;
};

/**
 * @brief 获取测试可用的有效世界实例（优先返回 GameWorld，无则回退首个非空 WorldContext）
 * @return UWorld 指针，无可用世界返回 nullptr
 */
[[nodiscard]] inline UWorld* GetAutomationTestWorld() {
    if (GEngine == nullptr) {
        return nullptr;
    }
    UWorld* FallbackWorld = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        UWorld* Candidate = Context.World();
        if (Candidate == nullptr) {
            continue;
        }
        if (Candidate->IsGameWorld()) {
            return Candidate;
        }
        if (FallbackWorld == nullptr) {
            FallbackWorld = Candidate;
        }
    }
    return FallbackWorld;
}

/**
 * @brief 调用 UE 公开接口查询指定 64 位槽位键是否已存在于引擎屏幕消息表
 * @param Key 64 位槽位键
 * @return 存在返回 true
 */
[[nodiscard]] inline bool HasKeyedScreenMessage(uint64 Key) {
    return GEngine != nullptr && GEngine->OnScreenDebugMessageExists(Key);
}

/**
 * @brief 获取目标世界的调试线段批处理组件（兼容 UE 5.1~5.8）
 * @param World 目标世界
 * @param bPersistent 是否获取持久化（Duration > 0）批处理组件
 * @return 线段批处理组件指针
 */
[[nodiscard]] inline ULineBatchComponent* GetWorldDebugLineBatcher(const UWorld* World, bool bPersistent) {
    if (World == nullptr) {
        return nullptr;
    }
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
    return World->GetLineBatcher(
        bPersistent ? UWorld::ELineBatcherType::WorldPersistent : UWorld::ELineBatcherType::World
    );
#else
    return bPersistent ? World->PersistentLineBatcher.Get() : World->LineBatcher.Get();
#endif
}

/**
 * @brief 获取目标世界全部单帧与持久化调试线段的总数
 * @param World 目标世界
 * @return 当前已入队的 BatchedLines 总数量
 */
[[nodiscard]] inline int32 GetTotalBatchedLineCount(const UWorld* World) {
    int32 Total = 0;
    if (const ULineBatchComponent* FrameBatcher = GetWorldDebugLineBatcher(World, false)) {
        Total += FrameBatcher->BatchedLines.Num();
    }
    if (const ULineBatchComponent* PersistentBatcher = GetWorldDebugLineBatcher(World, true)) {
        Total += PersistentBatcher->BatchedLines.Num();
    }
    return Total;
}

/**
 * @brief 清空目标世界的调试线段批处理缓存，防止测试间状态串味
 * @param World 目标世界
 */
inline void FlushWorldDebugLines(UWorld* World) {
    if (World == nullptr) {
        return;
    }
    if (ULineBatchComponent* FrameBatcher = GetWorldDebugLineBatcher(World, false)) {
        FrameBatcher->Flush();
    }
    if (ULineBatchComponent* PersistentBatcher = GetWorldDebugLineBatcher(World, true)) {
        PersistentBatcher->Flush();
    }
}

}  // namespace Game::Debug::Test

#endif  // WITH_AUTOMATION_TESTS
