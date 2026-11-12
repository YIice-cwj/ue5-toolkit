// Copyright Epic Games, Inc. All Rights Reserved.

#include "Config/FConfigPolicy.h"
#include "Config/GameCompileSwitches.h"
#include "Misc/StringBuilder.h"

/**
 * @brief 判定指定键名在当前编译开关与运行时环境下是否属于固有只读键
 * @param Name 配置键名
 * @return 固有只读返回 true
 */
bool FConfigPolicy::IsBuiltinReadOnlyKey(FName Name) noexcept {
    TStringBuilder<128> KeyBuilder;
    Name.AppendString(KeyBuilder);
    const FStringView KeyText = KeyBuilder.ToView();
    if (KeyText.StartsWith(TEXT("Env."))) {
        return true;
    }
    if constexpr (!Game::Config::bEnableDebugDraw) {
        if (KeyText.StartsWith(TEXT("Dev."))) {
            return true;
        }
    }
    if constexpr (!Game::Config::bEnableGM) {
        if (KeyText == TEXT("Feature.GM")) {
            return true;
        }
    }
    return false;
}
