/**
 * @file FSaveGameFileIO.h
 * @brief 存档底层文件 I/O 与防断电原子写保护（单一职责 · 路径计算 · 原子写入 · 容灾恢复）
 */

#pragma once

#include "CoreMinimal.h"
#include "Save/GameSaveArchive.h"

class USaveGame;

/**
 * @brief 存档文件底层 I/O 辅助工具
 */
class GAMEDATACORE_API FSaveGameFileIO {
public:
    /**
     * @brief 计算并获取指定槽位的正式存档文件绝对路径
     * @param SlotName 存档槽位名称
     * @return 正式存档文件的绝对路径字符串
     */
    static FString GetSaveSlotFilePath(const FString& SlotName);

    /**
     * @brief 计算并获取指定槽位的临时写入归档文件绝对路径（.sav.tmp）
     * @param SlotName 存档槽位名称
     * @return 临时写入文件的绝对路径字符串
     */
    static FString GetSaveSlotTempFilePath(const FString& SlotName);

    /**
     * @brief 槽位名称合法性与白名单校验（防止目录穿越与非法文件名）
     * @param SlotName 待校验的存档槽位名称
     * @param AllowedSlotNames 白名单集合（若为空集合则仅校验路径合法字符）
     * @return 合法通过返回 true，否则返回 false
     */
    static bool IsSlotNameAllowed(const FString& SlotName, const TSet<FString>& AllowedSlotNames);

    /**
     * @brief 检查指定槽位的磁盘物理文件是否存在（正式文件或临时文件）
     * @param SlotName 存档槽位名称
     * @return 存在任意对应存档物理文件返回 true，否则返回 false
     */
    static bool DoesSaveSlotExist(const FString& SlotName);

    /**
     * @brief 彻底删除指定槽位的物理文件（同时清理正式文件与临时文件）
     * @param SlotName 存档槽位名称
     * @return 删除成功返回 true，文件不存在或删除失败返回 false
     */
    static bool DeleteSaveSlot(const FString& SlotName);

    /**
     * @brief 将 USaveGame 对象序列化为二进制内存字节数组
     * @param SaveGameObject 待序列化的存档对象指针
     * @param OutBytes 输出的二进制字节数组
     * @return 序列化成功返回 true，对象无效或序列化异常返回 false
     */
    static bool SerializeObject(USaveGame* SaveGameObject, TArray<uint8>& OutBytes);

    /**
     * @brief 生成并签发槽位递增的写入代次序号（防止异步并发保存乱序覆写）
     * @param SlotName 存档槽位名称
     * @return 本次签发的唯一代次序号
     */
    static uint64 IssueSlotSaveGeneration(const FString& SlotName);

    /**
     * @brief 作废指定槽位的写入代次序号
     * @param SlotName 存档槽位名称
     */
    static void InvalidateSlotSaveGeneration(const FString& SlotName);

    /**
     * @brief 原子安全落盘写入字节（先写 .tmp 临时文件，校验成功后原子替换正式文件）
     * @param SlotName 存档槽位名称
     * @param Bytes 待落盘的二进制字节数组
     * @param SaveGeneration 签发的代次序号（非 0 时校验过期性）
     * @return 写入与原子重命名成功返回 true，否则返回 false
     */
    static bool WriteSlotBytes(const FString& SlotName, const TArray<uint8>& Bytes, uint64 SaveGeneration = 0);

    /**
     * @brief 读取指定槽位的二进制字节（正式文件损坏或缺失时自动自 .tmp 临时文件抢救恢复）
     * @param SlotName 存档槽位名称
     * @param OutBytes 输出的二进制字节数组
     * @return 读取成功返回 true，文件缺失或损坏且无法修复返回 false
     */
    static bool ReadSlotBytes(const FString& SlotName, TArray<uint8>& OutBytes);

    /**
     * @brief 校验二进制头部魔数与结构是否为合法的 USaveGame 格式
     * @param Bytes 待校验的二进制字节数组
     * @param SlotName 存档槽位名称（用于诊断日志）
     * @return 格式合法有效返回 true，否则返回 false
     */
    static bool HasValidSaveGameHeader(const TArray<uint8>& Bytes, const FString& SlotName);
};
