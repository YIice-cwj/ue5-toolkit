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

FString ResolveRegisteredTypeTag(const FConfigRegistry::FConfigVariant& ExistingVariant) {
    FString TypeTag = TEXT("string");
    Visit([&TypeTag](const auto& Stored) {
        using FStoredType = std::decay_t<decltype(Stored)>;
        if constexpr (std::is_same_v<FStoredType, bool>) {
            TypeTag = TEXT("bool");
        } else if constexpr (std::is_same_v<FStoredType, int32>) {
            TypeTag = TEXT("int32");
        } else if constexpr (std::is_same_v<FStoredType, int64>) {
            TypeTag = TEXT("int64");
        } else if constexpr (std::is_same_v<FStoredType, float>) {
            TypeTag = TEXT("float");
        } else if constexpr (std::is_same_v<FStoredType, double>) {
            TypeTag = TEXT("double");
        } else if constexpr (std::is_same_v<FStoredType, FString>) {
            TypeTag = TEXT("string");
        } else if constexpr (std::is_same_v<FStoredType, FName>) {
            TypeTag = TEXT("name");
        } else if constexpr (std::is_same_v<FStoredType, FVector>) {
            TypeTag = TEXT("vector");
        } else if constexpr (std::is_same_v<FStoredType, FRotator>) {
            TypeTag = TEXT("rotator");
        } else if constexpr (std::is_same_v<FStoredType, FGameplayTag>) {
            TypeTag = TEXT("tag");
        }
    }, ExistingVariant);
    return TypeTag;
}

}  // namespace

int32 FConfigSourceBridge::LoadIniSection(const FString& ConfigFilename, const FString& SectionName) {
    if (GConfig == nullptr || ConfigFilename.IsEmpty() || SectionName.IsEmpty()) {
        return 0;
    }

    TArray<FString> SectionLines;
    if (!GConfig->GetSection(*SectionName, SectionLines, ConfigFilename)) {
        return 0;
    }

    int32 AppliedCount = 0;
    for (const FString& Line : SectionLines) {
        FString KeyPart;
        FString ValuePart;
        if (Line.Split(TEXT("="), &KeyPart, &ValuePart)) {
            if (ApplySingleEntry(KeyPart, ValuePart)) {
                ++AppliedCount;
            }
        }
    }
    return AppliedCount;
}

int32 FConfigSourceBridge::ApplyKeyValueMap(const TMap<FString, FString>& KeyValues) {
    int32 AppliedCount = 0;
    for (const TPair<FString, FString>& Pair : KeyValues) {
        if (ApplySingleEntry(Pair.Key, Pair.Value)) {
            ++AppliedCount;
        }
    }
    return AppliedCount;
}

int32 FConfigSourceBridge::ApplyCommandLineOverrides(const TCHAR* CommandLine, const FString& ArgPrefix) {
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
            if (ApplySingleEntry(KeySpec, RawValue)) {
                ++AppliedCount;
            }
        }
    }
    return AppliedCount;
}

bool FConfigSourceBridge::ApplySingleEntry(const FString& RawKeySpec, const FString& RawValue) {
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
    FConfigRegistry& Registry = FConfigRegistry::Get();
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
            EffectiveTypeTag = ResolveRegisteredTypeTag(*Existing);
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
