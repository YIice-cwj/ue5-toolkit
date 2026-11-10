/**
 * @file FFastTagBitmask.cpp
 * @brief 64 位极速状态位掩码与标签字典实现
 */

#include "Containers/FFastTagBitmask.h"

/**
 * @brief 获取全局单例字典实例
 * @return 字典单例引用
 */
FFastTagDictionary& FFastTagDictionary::Get() {
    static FFastTagDictionary Instance;
    return Instance;
}

/**
 * @brief 注册或查询指定标签名称的位索引
 * @param TagName 标签名称
 * @return 分配的位索引 [0, 63]，注册满或非法名称返回 INDEX_NONE
 */
int32 FFastTagDictionary::RegisterOrFindTag(FName TagName) {
    if (TagName.IsNone()) {
        return INDEX_NONE;
    }

    FScopeLock Lock(&Mutex);

    if (const int32* ExistingIndex = TagNameToIndexMap.Find(TagName)) {
        return *ExistingIndex;
    }

    if (RegisteredTagNames.Num() >= MaxTagCount) {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("FFastTagDictionary::RegisterOrFindTag failed: Tag limit (%d) reached when registering '%s'."),
            MaxTagCount,
            *TagName.ToString()
        );
        return INDEX_NONE;
    }

    const int32 NewIndex = RegisteredTagNames.Num();
    RegisteredTagNames.Add(TagName);
    TagNameToIndexMap.Add(TagName, NewIndex);
    return NewIndex;
}

/**
 * @brief 查询已知标签名称的位索引
 * @param TagName 标签名称
 * @return 索引值，未注册返回 INDEX_NONE
 */
int32 FFastTagDictionary::FindTagIndex(FName TagName) const {
    if (TagName.IsNone()) {
        return INDEX_NONE;
    }

    FScopeLock Lock(&Mutex);
    if (const int32* FoundIndex = TagNameToIndexMap.Find(TagName)) {
        return *FoundIndex;
    }
    return INDEX_NONE;
}

/**
 * @brief 查询已知位索引对应的标签名称
 * @param BitIndex 索引值
 * @return 注册的名称，越界或未注册返回 NAME_None
 */
FName FFastTagDictionary::FindTagName(int32 BitIndex) const {
    FScopeLock Lock(&Mutex);
    if (BitIndex >= 0 && BitIndex < RegisteredTagNames.Num()) {
        return RegisteredTagNames[BitIndex];
    }
    return NAME_None;
}

/**
 * @brief 批量将标签名称列表转换为位掩码
 * @param TagNames 标签名称视图
 * @param OutAllSucceeded 可选输出标志：若所有标签均合法且未超出 64 上限则置为 true，否则置为 false
 * @return 聚合位掩码
 */
FFastTagBitmask FFastTagDictionary::MakeMask(TConstArrayView<FName> TagNames, bool* OutAllSucceeded) {
    FFastTagBitmask Mask;
    bool bAllSucceeded = true;
    for (const FName& Name : TagNames) {
        const int32 Index = RegisterOrFindTag(Name);
        if (Index != INDEX_NONE) {
            Mask.AddTag(Index);
        } else {
            bAllSucceeded = false;
        }
    }
    if (OutAllSucceeded != nullptr) {
        *OutAllSucceeded = bAllSucceeded;
    }
    return Mask;
}

/**
 * @brief 获取当前已注册的标签总数
 * @return 标签数量
 */
int32 FFastTagDictionary::Num() const {
    FScopeLock Lock(&Mutex);
    return RegisteredTagNames.Num();
}

/**
 * @brief 重置清空所有注册信息
 */
void FFastTagDictionary::Reset() {
    FScopeLock Lock(&Mutex);
    RegisteredTagNames.Reset();
    TagNameToIndexMap.Reset();
}
