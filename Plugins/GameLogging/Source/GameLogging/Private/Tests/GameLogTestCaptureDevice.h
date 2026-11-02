// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Misc/CString.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"

namespace Game::Log::Test {

/**
 * @brief 捕获匹配指定文本的真实日志，供测试断言分类与精确输出次数
 */
class FGameLogCaptureDevice final : public FOutputDevice {
public:
    /**
     * @brief 构造并挂载日志捕获器，清除引擎回放的历史日志计数
     * @param InMessageFilter 需要匹配的日志文本片段
     */
    explicit FGameLogCaptureDevice(const TCHAR* InMessageFilter)
        : MessageFilter(InMessageFilter) {
        FOutputDeviceRedirector::Get()->AddOutputDevice(this);
        FScopeLock ScopeLock(&Lock);
        TotalCount = 0;
        LastCategory = NAME_None;
    }

    /**
     * @brief 析构并解除日志捕获器挂载
     */
    ~FGameLogCaptureDevice() override {
        FOutputDeviceRedirector::Get()->RemoveOutputDevice(this);
    }

    /**
     * @brief 捕获匹配文本的日志分类并累加输出次数
     * @param Text 日志文本
     * @param Verbosity 日志级别
     * @param Category 日志分类
     */
    void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override {
        if (FCString::Strstr(Text, *MessageFilter) != nullptr) {
            FScopeLock ScopeLock(&Lock);
            ++TotalCount;
            LastCategory = Category;
        }
    }

    /**
     * @brief 声明捕获器支持多线程同时写入
     * @return 始终返回 true
     */
    bool CanBeUsedOnMultipleThreads() const override {
        return true;
    }

    /**
     * @brief 获取匹配日志的精确输出次数
     * @return 输出次数
     */
    [[nodiscard]] int32 GetTotalCount() const {
        FScopeLock ScopeLock(&Lock);
        return TotalCount;
    }

    /**
     * @brief 获取最近一次匹配日志的分类
     * @return 日志分类名
     */
    [[nodiscard]] FName GetLastCategory() const {
        FScopeLock ScopeLock(&Lock);
        return LastCategory;
    }

private:
    FString MessageFilter;
    mutable FCriticalSection Lock;
    int32 TotalCount = 0;
    FName LastCategory = NAME_None;
};

} // namespace Game::Log::Test
