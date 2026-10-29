// Copyright Epic Games, Inc. All Rights Reserved.

#include "Logging/FModuleLogRegistrar.h"
#include "Logging/FGameLogRegistry.h"
#include "Misc/CString.h"

namespace Game::Log {

namespace {

/**
 * @brief 拷贝 ANSI 模块名（含结尾符），调用方临时缓冲区亦可安全注册
 * @param InName 源名称
 * @return 含结尾符的模块名字符数组
 */
[[nodiscard]] static TArray<ANSICHAR> MakeRegistrarAnsiName(const char* InName) {
    const int32 Length = FCStringAnsi::Strlen(InName);
    TArray<ANSICHAR> Result;
    Result.SetNumUninitialized(Length + 1);
    FCStringAnsi::Strncpy(Result.GetData(), InName, Length + 1);
    return Result;
}

} // namespace

FModuleLogRegistrar::FModuleLogRegistrar(const char* InModuleName, FLogCategoryBase* InCategory) {
    if (InModuleName != nullptr && InCategory != nullptr) {
        ModuleName = MakeRegistrarAnsiName(InModuleName);
        FGameLogRegistry::RegisterCategory(ModuleName.GetData(), InCategory);
    }
}

FModuleLogRegistrar::~FModuleLogRegistrar() {
    if (!ModuleName.IsEmpty()) {
        FGameLogRegistry::UnregisterCategory(ModuleName.GetData());
    }
}

} // namespace Game::Log

