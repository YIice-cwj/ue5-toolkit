/**
 * @file FBootstrapRetryGate.cpp
 * @brief GAS 引导限次重试闸门实现
 */

#include "Ability/FBootstrapRetryGate.h"

void FBootstrapRetryGate::Configure(float InIntervalSeconds, int32 InMaxRetries) {
    IntervalSeconds = InIntervalSeconds;
    MaxRetries = InMaxRetries;
}

bool FBootstrapRetryGate::Advance() {
    ++Attempt;
    return Attempt <= MaxRetries;
}

void FBootstrapRetryGate::Reset() {
    Attempt = 0;
}

int32 FBootstrapRetryGate::GetAttempt() const {
    return Attempt;
}

int32 FBootstrapRetryGate::GetMaxRetries() const {
    return MaxRetries;
}

float FBootstrapRetryGate::GetIntervalSeconds() const {
    return IntervalSeconds;
}
