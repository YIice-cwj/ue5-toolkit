// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file FRemoteLogReentrancyScope.cpp
 * @brief 内部线程局部重入安全守卫实现
 */

#include "Remote/FRemoteLogReentrancyScope.h"

namespace Game::Log::Private {

namespace {
thread_local bool bThreadIsHandlingRemoteLog = false;
} // namespace

FRemoteLogReentrancyScope::FRemoteLogReentrancyScope() noexcept
    : bPreviousState(bThreadIsHandlingRemoteLog) {
    bThreadIsHandlingRemoteLog = true;
}

FRemoteLogReentrancyScope::~FRemoteLogReentrancyScope() noexcept {
    bThreadIsHandlingRemoteLog = bPreviousState;
}

bool FRemoteLogReentrancyScope::IsHandling() noexcept {
    return bThreadIsHandlingRemoteLog;
}

} // namespace Game::Log::Private
