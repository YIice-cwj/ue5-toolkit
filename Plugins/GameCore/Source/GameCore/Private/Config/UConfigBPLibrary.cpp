/**
 * @file UConfigBPLibrary.cpp
 * @brief 配置注册表蓝图桥接库实现（支持 WorldContext 穿透隔离）
 */

#include "Config/UConfigBPLibrary.h"

#include "Config/FConfigRegistry.h"

/**
 * @brief 读取布尔配置项
 * @param Key 配置键名
 * @param bDefaultValue 未注册时的默认回退值
 * @param WorldContextObject 世界上下文对象
 * @return 当前生效的布尔配置值
 */
bool UConfigBPLibrary::GetConfigBool(FName Key, bool bDefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<bool>(Key, bDefaultValue);
}

/**
 * @brief 读取 32 位有符号整数配置项
 * @param Key 配置键名
 * @param DefaultValue 未注册时的默认回退值
 * @param WorldContextObject 世界上下文对象
 * @return 当前生效的 32 位整数配置值
 */
int32 UConfigBPLibrary::GetConfigInt(FName Key, int32 DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<int32>(Key, DefaultValue);
}

/**
 * @brief 读取 64 位有符号整数配置项
 * @param Key 配置键名
 * @param DefaultValue 未注册时的默认回退值
 * @param WorldContextObject 世界上下文对象
 * @return 当前生效的 64 位整数配置值
 */
int64 UConfigBPLibrary::GetConfigInt64(FName Key, int64 DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<int64>(Key, DefaultValue);
}

/**
 * @brief 读取单精度浮点配置项
 * @param Key 配置键名
 * @param DefaultValue 未注册时的默认回退值
 * @param WorldContextObject 世界上下文对象
 * @return 当前生效的单精度浮点配置值
 */
float UConfigBPLibrary::GetConfigFloat(FName Key, float DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<float>(Key, DefaultValue);
}

/**
 * @brief 读取双精度浮点配置项
 * @param Key 配置键名
 * @param DefaultValue 未注册时的默认回退值
 * @param WorldContextObject 世界上下文对象
 * @return 当前生效的双精度浮点配置值
 */
double UConfigBPLibrary::GetConfigDouble(FName Key, double DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<double>(Key, DefaultValue);
}

/**
 * @brief 读取字符串配置项
 * @param Key 配置键名
 * @param DefaultValue 未注册时的默认回退值
 * @param WorldContextObject 世界上下文对象
 * @return 当前生效的字符串配置值
 */
FString UConfigBPLibrary::GetConfigString(FName Key, const FString& DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<FString>(Key, DefaultValue);
}

/**
 * @brief 读取 FName 标识配置项
 * @param Key 配置键名
 * @param DefaultValue 未注册时的默认回退值
 * @param WorldContextObject 世界上下文对象
 * @return 当前生效的 FName 配置值
 */
FName UConfigBPLibrary::GetConfigName(FName Key, FName DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<FName>(Key, DefaultValue);
}

/**
 * @brief 读取三维向量配置项
 * @param Key 配置键名
 * @param DefaultValue 未注册时的默认回退值
 * @param WorldContextObject 世界上下文对象
 * @return 当前生效的三维向量配置值
 */
FVector UConfigBPLibrary::GetConfigVector(FName Key, FVector DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<FVector>(Key, DefaultValue);
}

/**
 * @brief 读取欧拉旋转量配置项
 * @param Key 配置键名
 * @param DefaultValue 未注册时的默认回退值
 * @param WorldContextObject 世界上下文对象
 * @return 当前生效的旋转量配置值
 */
FRotator UConfigBPLibrary::GetConfigRotator(FName Key, FRotator DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<FRotator>(Key, DefaultValue);
}

/**
 * @brief 读取 GameplayTag 配置项
 * @param Key 配置键名
 * @param DefaultValue 未注册时的默认回退值
 * @param WorldContextObject 世界上下文对象
 * @return 当前生效的 GameplayTag 配置值
 */
FGameplayTag UConfigBPLibrary::GetConfigGameplayTag(FName Key, FGameplayTag DefaultValue, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).GetConfig<FGameplayTag>(Key, DefaultValue);
}

/**
 * @brief 查询指定配置键是否已在注册表中注册
 * @param Key 配置键名
 * @param WorldContextObject 世界上下文对象
 * @return 已注册返回 true
 */
bool UConfigBPLibrary::IsConfigRegistered(FName Key, const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).IsRegistered(Key);
}

/**
 * @brief 覆盖写入布尔配置项
 * @param Key 配置键名
 * @param bValue 待写入的布尔值
 * @param WorldContextObject 世界上下文对象
 */
void UConfigBPLibrary::SetConfigBool(FName Key, bool bValue, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<bool>(Key, bValue);
}

/**
 * @brief 覆盖写入 32 位有符号整数配置项
 * @param Key 配置键名
 * @param Value 待写入的 32 位整数值
 * @param WorldContextObject 世界上下文对象
 */
void UConfigBPLibrary::SetConfigInt(FName Key, int32 Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<int32>(Key, Value);
}

/**
 * @brief 覆盖写入 64 位有符号整数配置项
 * @param Key 配置键名
 * @param Value 待写入的 64 位整数值
 * @param WorldContextObject 世界上下文对象
 */
void UConfigBPLibrary::SetConfigInt64(FName Key, int64 Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<int64>(Key, Value);
}

/**
 * @brief 覆盖写入单精度浮点配置项
 * @param Key 配置键名
 * @param Value 待写入的单精度浮点值
 * @param WorldContextObject 世界上下文对象
 */
void UConfigBPLibrary::SetConfigFloat(FName Key, float Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<float>(Key, Value);
}

/**
 * @brief 覆盖写入双精度浮点配置项
 * @param Key 配置键名
 * @param Value 待写入的双精度浮点值
 * @param WorldContextObject 世界上下文对象
 */
void UConfigBPLibrary::SetConfigDouble(FName Key, double Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<double>(Key, Value);
}

/**
 * @brief 覆盖写入字符串配置项
 * @param Key 配置键名
 * @param Value 待写入的字符串值
 * @param WorldContextObject 世界上下文对象
 */
void UConfigBPLibrary::SetConfigString(FName Key, const FString& Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<FString>(Key, Value);
}

/**
 * @brief 覆盖写入 FName 配置项
 * @param Key 配置键名
 * @param Value 待写入的 FName 值
 * @param WorldContextObject 世界上下文对象
 */
void UConfigBPLibrary::SetConfigName(FName Key, FName Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<FName>(Key, Value);
}

/**
 * @brief 覆盖写入三维向量配置项
 * @param Key 配置键名
 * @param Value 待写入的三维向量值
 * @param WorldContextObject 世界上下文对象
 */
void UConfigBPLibrary::SetConfigVector(FName Key, FVector Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<FVector>(Key, Value);
}

/**
 * @brief 覆盖写入欧拉旋转量配置项
 * @param Key 配置键名
 * @param Value 待写入的旋转量值
 * @param WorldContextObject 世界上下文对象
 */
void UConfigBPLibrary::SetConfigRotator(FName Key, FRotator Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<FRotator>(Key, Value);
}

/**
 * @brief 覆盖写入 GameplayTag 配置项
 * @param Key 配置键名
 * @param Value 待写入的 GameplayTag 值
 * @param WorldContextObject 世界上下文对象
 */
void UConfigBPLibrary::SetConfigGameplayTag(FName Key, FGameplayTag Value, const UObject* WorldContextObject) {
    FConfigRegistry::Get(WorldContextObject).SetConfig<FGameplayTag>(Key, Value);
}

/**
 * @brief 获取配置注册表当前全局递增版本号
 * @param WorldContextObject 世界上下文对象
 * @return 当前版本号
 */
int64 UConfigBPLibrary::GetConfigVersion(const UObject* WorldContextObject) {
    return static_cast<int64>(FConfigRegistry::Get(WorldContextObject).GetVersion());
}

/**
 * @brief 获取配置注册表当前已注册的配置键总数
 * @param WorldContextObject 世界上下文对象
 * @return 已注册配置项总数
 */
int32 UConfigBPLibrary::GetRegisteredConfigCount(const UObject* WorldContextObject) {
    return FConfigRegistry::Get(WorldContextObject).Num();
}
