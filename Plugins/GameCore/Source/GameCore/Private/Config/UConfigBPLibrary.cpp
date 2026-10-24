/**
 * @file UConfigBPLibrary.cpp
 * @brief 配置注册表蓝图桥接库实现
 */

#include "Config/UConfigBPLibrary.h"

#include "Config/FConfigRegistry.h"

bool UConfigBPLibrary::GetConfigBool(FName Key, bool bDefaultValue) {
    return FConfigRegistry::Get().GetConfig<bool>(Key, bDefaultValue);
}

int32 UConfigBPLibrary::GetConfigInt(FName Key, int32 DefaultValue) {
    return FConfigRegistry::Get().GetConfig<int32>(Key, DefaultValue);
}

int64 UConfigBPLibrary::GetConfigInt64(FName Key, int64 DefaultValue) {
    return FConfigRegistry::Get().GetConfig<int64>(Key, DefaultValue);
}

float UConfigBPLibrary::GetConfigFloat(FName Key, float DefaultValue) {
    return FConfigRegistry::Get().GetConfig<float>(Key, DefaultValue);
}

double UConfigBPLibrary::GetConfigDouble(FName Key, double DefaultValue) {
    return FConfigRegistry::Get().GetConfig<double>(Key, DefaultValue);
}

FString UConfigBPLibrary::GetConfigString(FName Key, const FString& DefaultValue) {
    return FConfigRegistry::Get().GetConfig<FString>(Key, DefaultValue);
}

FName UConfigBPLibrary::GetConfigName(FName Key, FName DefaultValue) {
    return FConfigRegistry::Get().GetConfig<FName>(Key, DefaultValue);
}

FVector UConfigBPLibrary::GetConfigVector(FName Key, FVector DefaultValue) {
    return FConfigRegistry::Get().GetConfig<FVector>(Key, DefaultValue);
}

FRotator UConfigBPLibrary::GetConfigRotator(FName Key, FRotator DefaultValue) {
    return FConfigRegistry::Get().GetConfig<FRotator>(Key, DefaultValue);
}

FGameplayTag UConfigBPLibrary::GetConfigGameplayTag(FName Key, FGameplayTag DefaultValue) {
    return FConfigRegistry::Get().GetConfig<FGameplayTag>(Key, DefaultValue);
}

bool UConfigBPLibrary::IsConfigRegistered(FName Key) {
    return FConfigRegistry::Get().IsRegistered(Key);
}

void UConfigBPLibrary::SetConfigBool(FName Key, bool bValue) {
    FConfigRegistry::Get().SetConfig<bool>(Key, bValue);
}

void UConfigBPLibrary::SetConfigInt(FName Key, int32 Value) {
    FConfigRegistry::Get().SetConfig<int32>(Key, Value);
}

void UConfigBPLibrary::SetConfigInt64(FName Key, int64 Value) {
    FConfigRegistry::Get().SetConfig<int64>(Key, Value);
}

void UConfigBPLibrary::SetConfigFloat(FName Key, float Value) {
    FConfigRegistry::Get().SetConfig<float>(Key, Value);
}

void UConfigBPLibrary::SetConfigDouble(FName Key, double Value) {
    FConfigRegistry::Get().SetConfig<double>(Key, Value);
}

void UConfigBPLibrary::SetConfigString(FName Key, const FString& Value) {
    FConfigRegistry::Get().SetConfig<FString>(Key, Value);
}

void UConfigBPLibrary::SetConfigName(FName Key, FName Value) {
    FConfigRegistry::Get().SetConfig<FName>(Key, Value);
}

void UConfigBPLibrary::SetConfigVector(FName Key, FVector Value) {
    FConfigRegistry::Get().SetConfig<FVector>(Key, Value);
}

void UConfigBPLibrary::SetConfigRotator(FName Key, FRotator Value) {
    FConfigRegistry::Get().SetConfig<FRotator>(Key, Value);
}

void UConfigBPLibrary::SetConfigGameplayTag(FName Key, FGameplayTag Value) {
    FConfigRegistry::Get().SetConfig<FGameplayTag>(Key, Value);
}
