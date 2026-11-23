/**
 * @file FSaveGameFileIO.h
 * @brief 存档底层文件 I/O 与防断电原子写保护（单一职责 · 路径计算 · 锁分离原子写入 · 容灾恢复）
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/TGameResult.h"

class USaveGame;

/**
 * @brief 存档文件底层 I/O 辅助工具（纯静态类，禁止实例化）
 */
class GAMEDATACORE_API FSaveGameFileIO {
public:
    FSaveGameFileIO() = delete;

    /**
     * @brief 计算并获取指定槽位的正式存档文件绝对路径
     * @param SlotName 存档槽位名称
     * @return 正式存档文件的绝对路径字符串
     */
    [[nodiscard]] static FString GetSaveSlotFilePath(const FString& SlotName);

    /**
     * @brief 计算并获取指定槽位的临时写入归档文件绝对路径（.sav.tmp）
     * @param SlotName 存档槽位名称
     * @return 临时写入文件的绝对路径字符串
     */
    [[nodiscard]] static FString GetSaveSlotTempFilePath(const FString& SlotName);

    /**
     * @brief 槽位名称合法性与白名单校验（防止目录穿越与非法文件名）
     * @param SlotName 待校验的存档槽位名称
     * @param AllowedSlotNames 白名单集合（若为空集合则仅校验路径合法字符）
     * @return 合法通过返回 true，否则返回 false
     */
    [[nodiscard]] static bool IsSlotNameAllowed(const FString& SlotName, const TSet<FString>& AllowedSlotNames);

    /**
     * @brief 检查指定槽位的磁盘物理文件是否存在（正式文件或临时文件）
     * @param SlotName 存档槽位名称
     * @return 存在任意对应存档物理文件返回 true，否则返回 false
     */
    [[nodiscard]] static bool DoesSaveSlotExist(const FString& SlotName);

    /**
     * @brief 原子失效在途写入代次并删除指定槽位的正式文件与临时文件（同锁内防止写穿与 ABA 复活）
     * @param SlotName 存档槽位名称
     * @return 成功删除返回 Success，未找到对应文件或删除失败返回含 Game::DataErrorCodes 的 Failure
     */
    [[nodiscard]] static TGameStatusResult<void> DeleteSaveSlot(const FString& SlotName);

    /**
     * @brief 将 USaveGame 对象序列化为二进制内存字节数组
     * @param SaveGameObject 待序列化的存档对象指针
     * @return 成功返回二进制字节数组，对象无效或序列化异常返回含 Game::DataErrorCodes 的 Failure
     */
    [[nodiscard]] static TGameStatusResult<TArray<uint8>> SerializeObject(USaveGame* SaveGameObject);

    /**
     * @brief 生成并签发槽位递增的写入代次序号（仅锁内存代次表，不阻塞于后台磁盘 I/O）
     * @param SlotName 存档槽位名称
     * @return 本次签发的唯一代次序号；删除后仍沿高水位严格递增，不复用历史代次
     */
    [[nodiscard]] static uint64 IssueSlotSaveGeneration(const FString& SlotName);

    /**
     * @brief 使槽位当前已签发的全部在途保存代次失效（单调推进签发与已提交高水位）
     * @param SlotName 存档槽位名称
     */
    static void InvalidateSlotSaveGeneration(const FString& SlotName);

    /**
     * @brief 检查给定写入代次是否已被该槽位更新的签发代次或删槽失效水位超越
     * @param SlotName 存档槽位名称
     * @param SaveGeneration 待校验的写入代次序号
     * @return 已被更新写入或删槽超越返回 true
     */
    [[nodiscard]] static bool IsSlotSaveGenerationSuperseded(const FString& SlotName, uint64 SaveGeneration);

    /**
     * @brief 按槽位加锁执行原子落盘写入（先写 .tmp 临时文件，再原子替换正式文件；Move 失败保留有效 .tmp 供灾备恢复）
     * @param SlotName 存档槽位名称
     * @param Bytes 待落盘的二进制字节数组
     * @param SaveGeneration 签发的代次序号（非 0 时校验高水位过期性，过期写安全跳过并返回 Success）
     * @param bOutSkippedStale 可选输出：因代次落后于已提交高水位而跳过物理写盘时置为 true，实际完成写盘置为 false
     * @return 成功或过期安全跳过返回 Success，I/O 失败返回含 Game::DataErrorCodes 的 Failure
     */
    [[nodiscard]] static TGameStatusResult<void> WriteSlotBytes(
        const FString& SlotName,
        const TArray<uint8>& Bytes,
        uint64 SaveGeneration = 0,
        bool* bOutSkippedStale = nullptr
    );

    /**
     * @brief 读取指定槽位的二进制字节（正式文件损坏或缺失时自动自 .tmp 临时文件抢救恢复）
     * @param SlotName 存档槽位名称
     * @return 成功返回包含二进制数组的 StatusResult，失败返回含 Game::DataErrorCodes 的 Failure
     */
    [[nodiscard]] static TGameStatusResult<TArray<uint8>> ReadSlotBytes(const FString& SlotName);

    /**
     * @brief 校验二进制前导魔数（0x53415647）与最小头长度（>= 64 字节）是否符合 UE USaveGame 格式
     * @param Bytes 待校验的二进制字节数组
     * @param SlotName 存档槽位名称（用于诊断日志）
     * @return 前导魔数与最小长度校验通过返回 true，否则返回 false
     */
    [[nodiscard]] static bool HasValidSaveGameHeader(const TArray<uint8>& Bytes, const FString& SlotName);
};
