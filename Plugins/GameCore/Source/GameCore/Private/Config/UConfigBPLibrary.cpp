/**
 * @file UConfigBPLibrary.cpp
 * @brief 配置注册表蓝图桥接库实现（支持 WorldContext 穿透隔离）
 */

#include "Config/UConfigBPLibrary.h"

#include "Config/FConfigRegistry.h"

bool UConfigBPLibrary::GetConfigBool(FName Key, bool bDefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<bool>(Key, bDefaultValue);
}

int32 UConfigBPLibrary::GetConfigInt(FName Key, int32 DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<int32>(Key, DefaultValue);
}

int64 UConfigBPLibrary::GetConfigInt64(FName Key, int64 DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<int64>(Key, DefaultValue);
}

float UConfigBPLibrary::GetConfigFloat(FName Key, float DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<float>(Key, DefaultValue);
}

double UConfigBPLibrary::GetConfigDouble(FName Key, double DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<double>(Key, DefaultValue);
}

FString UConfigBPLibrary::GetConfigString(FName Key, const FString& DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<FString>(Key, DefaultValue);
}

FName UConfigBPLibrary::GetConfigName(FName Key, FName DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<FName>(Key, DefaultValue);
}

FVector UConfigBPLibrary::GetConfigVector(FName Key, FVector DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<FVector>(Key, DefaultValue);
}

FRotator UConfigBPLibrary::GetConfigRotator(FName Key, FRotator DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<FRotator>(Key, DefaultValue);
}

FGameplayTag UConfigBPLibrary::GetConfigGameplayTag(FName Key, FGameplayTag DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<FGameplayTag>(Key, DefaultValue);
}

bool UConfigBPLibrary::IsConfigRegistered(FName Key, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).IsRegistered(Key);
}

void UConfigBPLibrary::SetConfigBool(FName Key, bool bValue, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<bool>(Key, bValue);
}

void UConfigBPLibrary::SetConfigInt(FName Key, int32 Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<int32>(Key, Value);
}

void UConfigBPLibrary::SetConfigInt64(FName Key, int64 Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<int64>(Key, Value);
}

void UConfigBPLibrary::SetConfigFloat(FName Key, float Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<float>(Key, Value);
}

void UConfigBPLibrary::SetConfigDouble(FName Key, double Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<double>(Key, Value);
}

void UConfigBPLibrary::SetConfigString(FName Key, const FString& Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<FString>(Key, Value);
}

void UConfigBPLibrary::SetConfigName(FName Key, FName Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<FName>(Key, Value);
}

void UConfigBPLibrary::SetConfigVector(FName Key, FVector Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<FVector>(Key, Value);
}

void UConfigBPLibrary::SetConfigRotator(FName Key, FRotator Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<FRotator>(Key, Value);
}

void UConfigBPLibrary::SetConfigGameplayTag(FName Key, FGameplayTag Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<FGameplayTag>(Key, Value);
}

int64 UConfigBPLibrary::GetConfigVersion(const UObject* WorldContextObject) {
    return static_cast<int64>(FConfigRegistry::Get(WorldContextObject).GetVersion());
}

int32 UConfigBPLibrary::GetRegisteredConfigCount(const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).Num();
}
