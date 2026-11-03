// Copyright Epic Games, Inc. All Rights Reserved.

#include "Logging/FModuleLogRegistrar.h"
#include "Logging/FGameLogRegistry.h"

namespace Game::Log {

FModuleLogRegistrar::FModuleLogRegistrar(const char* InModuleName, FLogCategoryBase* InCategory) {
    if (InModuleName != nullptr && InModuleName[0] != '\0' && InCategory != nullptr) {
        ModuleName = FGameLogRegistry::MakeAnsiName(InModuleName);
        Category = InCategory;
        PreviousCategory = FGameLogRegistry::RegisterScopedCategory(ModuleName.GetData(), InCategory);
    }
}

FModuleLogRegistrar::~FModuleLogRegistrar() {
    if (Category != nullptr) {
        FGameLogRegistry::UnregisterScopedCategory(ModuleName.GetData(), Category, PreviousCategory);
    }
}

} // namespace Game::Log

