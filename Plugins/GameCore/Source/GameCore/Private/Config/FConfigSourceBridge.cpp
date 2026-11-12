/**
 * @file FConfigSourceBridge.cpp
 * @brief 外部配置源（INI 节与命令行参数）向 FConfigRegistry 的强类型安全注入桥接实现
 */

#include "Config/FConfigSourceBridge.h"

#include <type_traits>

#include "Config/FConfigRegistry.h"
#include "Config/GameCompileSwitches.h"
#include "GameplayTagsManager.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameConfigBridge, Log, All);

namespace {

void TokenizeQuoteAware(const TCHAR* Stream, TArray<FString>& OutTokens) {
    OutTokens.Reset();
    if (Stream == nullptr) {
        return;
    }

    FString Current;
    bool bInQuotes = false;
    for (const TCHAR* Cursor = Stream; *Cursor != TEXT('\0'); ++Cursor) {
        const TCHAR Ch = *Cursor;
        if (Ch == TEXT('\"')) {
            bInQuotes = !bInQuotes;
            Current.AppendChar(Ch);
            continue;
        }
        if (!bInQuotes && FChar::IsWhitespace(Ch)) {
            if (!Current.IsEmpty()) {
                OutTokens.Add(MoveTemp(Current));
                Current.Reset();
            }
            continue;
        }
        Current.AppendChar(Ch);
    }
    if (!Current.IsEmpty()) {
        OutTokens.Add(MoveTemp(Current));
    }
}

FString StripMatchingQuotes(const FString& Input) {
    FString Trimmed = Input.TrimStartAndEnd();
    if (Trimmed.Len() >= 2 && Trimmed.StartsWith(TEXT("\"")) && Trimmed.EndsWith(TEXT("\""))) {
        return Trimmed.Mid(1, Trimmed.Len() - 2);
    }
    return Trimmed;
}

bool TrySetTypedValue(FConfigRegistry& Registry, const FName& KeyName, const FString& TypeTag, const FString& CleanValue) {
    if (TypeTag.Equals(TEXT("bool"), ESearchCase::IgnoreCase)) {
        bool Parsed = false;
        if (!LexTryParseString(Parsed, *CleanValue)) {
            return false;
        }
        return Registry.SetConfig<bool>(KeyName, Parsed) == ESetConfigResult::Applied;
    }
    if (TypeTag.Equals(TEXT("int32"), ESearchCase::IgnoreCase) || TypeTag.Equals(TEXT("int"), ESearchCase::IgnoreCase)) {
        int32 Parsed = 0;
        if (!LexTryParseString(Parsed, *CleanValue)) {
            return false;
        }
        return Registry.SetConfig<int32>(KeyName, Parsed) == ESetConfigResult::Applied;
    }
    if (TypeTag.Equals(TEXT("int64"), ESearchCase::IgnoreCase)) {
        int64 Parsed = 0;
        if (!LexTryParseString(Parsed, *CleanValue)) {
            return false;
        }
        return Registry.SetConfig<int64>(KeyName, Parsed) == ESetConfigResult::Applied;
    }
    if (TypeTag.Equals(TEXT("float"), ESearchCase::IgnoreCase)) {
        float Parsed = 0.0f;
        if (!LexTryParseString(Parsed, *CleanValue)) {
            return false;
        }
        return Registry.SetConfig<float>(KeyName, Parsed) == ESetConfigResult::Applied;
    }
    if (TypeTag.Equals(TEXT("double"), ESearchCase::IgnoreCase)) {
        double Parsed = 0.0;
        if (!LexTryParseString(Parsed, *CleanValue)) {
            return false;
        }
        return Registry.SetConfig<double>(KeyName, Parsed) == ESetConfigResult::Applied;
    }
    if (TypeTag.Equals(TEXT("string"), ESearchCase::IgnoreCase) || TypeTag.Equals(TEXT("FString"), ESearchCase::IgnoreCase)) {
        return Registry.SetConfig<FString>(KeyName, CleanValue) == ESetConfigResult::Applied;
    }
    if (TypeTag.Equals(TEXT("name"), ESearchCase::IgnoreCase) || TypeTag.Equals(TEXT("FName"), ESearchCase::IgnoreCase)) {
        return Registry.SetConfig<FName>(KeyName, FName(*CleanValue)) == ESetConfigResult::Applied;
    }
    if (TypeTag.Equals(TEXT("vector"), ESearchCase::IgnoreCase) || TypeTag.Equals(TEXT("FVector"), ESearchCase::IgnoreCase)) {
        FVector Parsed = FVector::ZeroVector;
        if (!Parsed.InitFromString(CleanValue)) {
            return false;
        }
        return Registry.SetConfig<FVector>(KeyName, Parsed) == ESetConfigResult::Applied;
    }
    if (TypeTag.Equals(TEXT("rotator"), ESearchCase::IgnoreCase) || TypeTag.Equals(TEXT("FRotator"), ESearchCase::IgnoreCase)) {
        FRotator Parsed = FRotator::ZeroRotator;
        if (!Parsed.InitFromString(CleanValue)) {
            return false;
        }
        return Registry.SetConfig<FRotator>(KeyName, Parsed) == ESetConfigResult::Applied;
    }
    if (TypeTag.Equals(TEXT("tag"), ESearchCase::IgnoreCase) || TypeTag.Equals(TEXT("FGameplayTag"), ESearchCase::IgnoreCase)) {
        const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*CleanValue), false);
        if (!Tag.IsValid()) {
            return false;
        }
        return Registry.SetConfig<FGameplayTag>(KeyName, Tag) == ESetConfigResult::Applied;
    }
    return false;
}

}  // namespace

/**
 * @brief 从指定 INI 配置文件的指定节读取全部键值对并注入 FConfigRegistry
 * @param ConfigFilename 目标 INI 文件名（如 GGameIni）
 * @param SectionName 目标节名称（如 TEXT("GameCore.Config")）
 * @param WorldContextObject 关联的世界上下文对象（为空表示全局根注册表）
 * @return 成功解析并写入的配置项数量
 */
int32 FConfigSourceBridge::LoadIniSection(const FString& ConfigFilename, const FString& SectionName, const UObject* WorldContextObject) {
    if (GConfig == nullptr || ConfigFilename.IsEmpty() || SectionName.IsEmpty()) {
        return 0;
    }

    TArray<FString> SectionLines;
    if (!GConfig->GetSection(*SectionName, SectionLines, ConfigFilename)) {
        return 0;
    }

    int32 AppliedCount = 0;
    for (const FString& Line : SectionLines) {
        const FString TrimmedLine = Line.TrimStartAndEnd();
        if (TrimmedLine.IsEmpty() || TrimmedLine.StartsWith(TEXT(";")) ||
            TrimmedLine.StartsWith(TEXT("#")) || TrimmedLine.StartsWith(TEXT("//"))) {
            continue;
        }
        FString KeyPart;
        FString ValuePart;
        if (TrimmedLine.Split(TEXT("="), &KeyPart, &ValuePart)) {
            if (ApplySingleEntry(KeyPart, ValuePart, WorldContextObject)) {
                ++AppliedCount;
            }
        }
    }
    return AppliedCount;
}

/**
 * @brief 批量解析内存键值对映射并注入 FConfigRegistry
 * @param KeyValues 键值对集合（键支持可选前缀如 int32:MaxPlayers、float:Speed 等）
 * @param WorldContextObject 关联的世界上下文对象（为空表示全局根注册表）
 * @return 成功解析并写入的配置项数量
 */
int32 FConfigSourceBridge::ApplyKeyValueMap(const TMap<FString, FString>& KeyValues, const UObject* WorldContextObject) {
    int32 AppliedCount = 0;
    for (const TPair<FString, FString>& Pair : KeyValues) {
        if (ApplySingleEntry(Pair.Key, Pair.Value, WorldContextObject)) {
            ++AppliedCount;
        }
    }
    return AppliedCount;
}

/**
 * @brief 解析命令行参数中带指定前缀的覆盖项（如 -Config.Combat.Crit=0.5）并注入 FConfigRegistry
 * @param CommandLine 原始命令行字符串
 * @param ArgPrefix 命令行参数匹配前缀（默认 TEXT("Config.")，自动兼容前导 -）
 * @param WorldContextObject 关联的世界上下文对象（为空表示全局根注册表）
 * @return 成功解析并写入的配置项数量
 */
int32 FConfigSourceBridge::ApplyCommandLineOverrides(const TCHAR* CommandLine, const FString& ArgPrefix, const UObject* WorldContextObject) {
    if (CommandLine == nullptr || ArgPrefix.IsEmpty()) {
        return 0;
    }

    FString NormalizedPrefix = ArgPrefix;
    if (NormalizedPrefix.StartsWith(TEXT("-"))) {
        NormalizedPrefix.RightChopInline(1);
    }
    if (NormalizedPrefix.IsEmpty()) {
        return 0;
    }

    int32 AppliedCount = 0;
    TArray<FString> Tokens;
    TokenizeQuoteAware(CommandLine, Tokens);
    for (const FString& Token : Tokens) {
        if (!Token.StartsWith(TEXT("-"))) {
            continue;
        }

        FString Body = Token.RightChop(1);
        if (Body.StartsWith(TEXT("-"))) {
            Body.RightChopInline(1);
        }

        if (!Body.StartsWith(NormalizedPrefix, ESearchCase::IgnoreCase)) {
            continue;
        }

        const FString Remainder = Body.RightChop(NormalizedPrefix.Len());
        FString KeySpec;
        FString RawValue;
        if (Remainder.Split(TEXT("="), &KeySpec, &RawValue)) {
            if (ApplySingleEntry(KeySpec, RawValue, WorldContextObject)) {
                ++AppliedCount;
            }
        }
    }
    return AppliedCount;
}

/**
 * @brief 解析单条外部键值规格并安全写入 FConfigRegistry（只读键与非法格式在边界直接拦截）
 * @param RawKeySpec 原始键规格（如 TEXT("Combat.Crit") 或 TEXT("float:Combat.Crit")）
 * @param RawValue 原始字符串值
 * @param WorldContextObject 关联的世界上下文对象（为空表示全局根注册表）
 * @return 解析合法且成功写入返回 true；只读拦截或格式非法返回 false
 */
bool FConfigSourceBridge::ApplySingleEntry(const FString& RawKeySpec, const FString& RawValue, const UObject* WorldContextObject) {
    const FString TrimmedSpec = RawKeySpec.TrimStartAndEnd();
    if (TrimmedSpec.IsEmpty()) {
        return false;
    }

    FString ExplicitTypeTag;
    FString CleanKeyString = TrimmedSpec;
    int32 ColonIndex = INDEX_NONE;
    if (TrimmedSpec.FindChar(TEXT(':'), ColonIndex)) {
        ExplicitTypeTag = TrimmedSpec.Left(ColonIndex).TrimStartAndEnd();
        CleanKeyString = TrimmedSpec.Mid(ColonIndex + 1).TrimStartAndEnd();
    }

    if (CleanKeyString.IsEmpty()) {
        return false;
    }

    const FName KeyName(*CleanKeyString);
    FConfigRegistry& Registry = FConfigRegistry::Get(WorldContextObject);
    if (Registry.IsReadOnly(KeyName)) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameConfigBridge, Warning,
                TEXT("【配置桥接】拦截对只读配置键 [%s] 的外部覆盖请求。"), *CleanKeyString);
        }
        return false;
    }

    const FString CleanValue = StripMatchingQuotes(RawValue);
    FString EffectiveTypeTag = ExplicitTypeTag;
    if (EffectiveTypeTag.IsEmpty()) {
        if (const TOptional<FConfigRegistry::FConfigVariant> Existing = Registry.GetRawConfigOptional(KeyName)) {
            EffectiveTypeTag = Game::Config::GetVariantTypeName(*Existing);
        } else {
            EffectiveTypeTag = TEXT("string");
        }
    }

    const bool bApplied = TrySetTypedValue(Registry, KeyName, EffectiveTypeTag, CleanValue);
    if (!bApplied) {
        if constexpr (Game::Config::bEnableLog) {
            UE_LOG(LogGameConfigBridge, Warning,
                TEXT("【配置桥接】外部配置项解析失败: 键=[%s], 期望类型=[%s], 原始值=[%s]"),
                *CleanKeyString, *EffectiveTypeTag, *RawValue);
        }
    }
    return bApplied;
}
