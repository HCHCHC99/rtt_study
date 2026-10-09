/**
 * *******************************************************************************
 * @file  param_manager.c
 * @brief Flash parameter storage engine with wear-leveling.
 *        Supports sequential append + CRC32 verification + rollback search.
 *
 *        Multi-instance: each caller provides its own Param_Config_t (sector
 *        range, struct layout, magic) and Param_Runtime_t (current position).
 *        No static state — the same code manages independent Flash regions.
 *
 *        RTOS 适配（相对裸机原版）：
 *        1. CRC32 改软件实现（原用 DDL 硬件 CRC，本项目 hc32_ll_crc.c 未启用）；
 *           算法等价：CRC-32/ISO-HDLC（RefIn/RefOut/XorOut 均使能，init=0xFFFFFFFF）。
 *        2. 临界区改 rt_hw_interrupt_disable/enable（保存标志成对恢复）。
 *        3. Init 扫描缓冲改静态分配（原栈上 tempBuf[256] 硬编码，超限爆栈），
 *           paramSize 超过 PARAM_MAX_RECORD_SIZE 直接返回参数错误。
 * *******************************************************************************
 */

#include "param_manager.h"
#include <string.h>
#include <rthw.h>   /* rt_hw_interrupt_disable/enable */

#define MAX_ERASE_LIFE          10000
#define MAX_WRITE_RETRY         7

/* Init 扫描用临时缓冲（单实例一次只扫一块，静态分配避免大栈占用） */
static uint8_t s_scanBuf[PARAM_MAX_RECORD_SIZE];

/*=============================================================================
 * Internal helpers
 *============================================================================*/

/**
 * @brief  Read a uint32_t field from buffer at given byte offset.
 */
static uint32_t GetField32(const void *pBuf, uint32_t offset)
{
    return *(const uint32_t *)((const uint8_t *)pBuf + offset);
}

/**
 * @brief  Write a uint32_t field to buffer at given byte offset.
 */
static void SetField32(void *pBuf, uint32_t offset, uint32_t value)
{
    *(uint32_t *)((uint8_t *)pBuf + offset) = value;
}

/**
 * @brief  Calculate CRC32 for parameter struct (bytes before checksum field).
 * @note   Software CRC-32/ISO-HDLC: poly(反射)=0xEDB88320, init=0xFFFFFFFF,
 *         RefIn/RefOut/XorOut enable —— 与原硬件 CRC 配置结果一致。
 */
static uint32_t CalcParamCRC(const void *pBuf, uint32_t paramSize, uint32_t checksumOffset)
{
    uint32_t crc = 0xFFFFFFFFU;
    const uint8_t *pData;
    uint32_t i;
    int b;

    if (pBuf == NULL || checksumOffset > paramSize) {
        return 0;
    }

    pData = (const uint8_t *)pBuf;
    for (i = 0U; i < checksumOffset; i++) {
        crc ^= pData[i];
        for (b = 0; b < 8; b++) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }

    return crc ^ 0xFFFFFFFFU;
}

/**
 * @brief  Update runtime debug info (called after init/save).
 */
static void UpdateRuntime(Param_Runtime_t *pRuntime, int32_t res, uint32_t paramSize)
{
    if (pRuntime == NULL) return;

    pRuntime->last_res = res;
    if (res == PARAM_OK) {
        pRuntime->save_count++;
    }
}

/*=============================================================================
 * Low-level Flash operations (thin wrappers around Adp/hc32_drv_flash)
 *============================================================================*/

static int32_t Internal_Erase(uint32_t address)
{
    HC32FLASH_STATUS status;
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    status = HC32FLASH_EraseSector(address);
    rt_hw_interrupt_enable(level);

    return (status == HC32FLASH_OK) ? PARAM_OK : PARAM_ERR;
}

static uint32_t Internal_ReadWord(uint32_t addr)
{
    uint32_t data;
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    data = HC32FLASH_ReaddWord(addr);
    rt_hw_interrupt_enable(level);

    return data;
}

static int32_t Internal_WriteWord(uint32_t addr, uint32_t data)
{
    HC32FLASH_STATUS status;
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    status = HC32FLASH_WritedWord_Check(addr, data);
    rt_hw_interrupt_enable(level);

    return (status == HC32FLASH_OK) ? PARAM_OK : PARAM_ERR;
}

static int32_t Internal_WriteBuffer(uint32_t addr, const uint32_t *buffer, uint32_t word_count)
{
    uint32_t i;
    for (i = 0; i < word_count; i++) {
        if (Internal_WriteWord(addr + i * 4, buffer[i]) != PARAM_OK) {
            return PARAM_ERR;
        }
    }
    return PARAM_OK;
}

static bool Internal_VerifyBuffer(uint32_t addr, const uint32_t *buffer, uint32_t word_count)
{
    uint32_t i;
    for (i = 0; i < word_count; i++) {
        if (Internal_ReadWord(addr + i * 4) != buffer[i]) {
            return false;
        }
    }
    return true;
}

/*=============================================================================
 * Public API
 *============================================================================*/

/**
 * @brief  Initialize parameter storage.
 *         Scans Flash sectors from secStart down to secEnd,
 *         finds the newest valid record (by sequence_id + head/tail magic + CRC),
 *         and loads it into RAM.
 */
int32_t Param_Init(const Param_Config_t *pConfig,
                   Param_Runtime_t *pRuntime,
                   void (*pSetDefaults)(void))
{
    uint32_t max_seq  = 0;
    uint32_t best_addr = 0;
    uint16_t best_sec  = pConfig->secStart;
    bool found = false;
    int  s;
    uint32_t paramWords;
    uint32_t headMagic;
    uint32_t tailMagic;
    uint32_t seq;
    uint32_t calcCrc;
    uint32_t storedCrc;

    if ((pConfig == NULL) || (pConfig->pParamBuf == NULL) || (pSetDefaults == NULL) || (pRuntime == NULL)) {
        return PARAM_ERR_INVD_PARAM;
    }

    if ((pConfig->paramSize == 0U) || (pConfig->paramSize > PARAM_MAX_RECORD_SIZE) ||
        ((pConfig->paramSize % 4U) != 0U)) {
        PARAM_PRINT("Param init err: paramSize=%lu invalid (max %u, 4-byte aligned)",
                    (unsigned long)pConfig->paramSize, (unsigned)PARAM_MAX_RECORD_SIZE);
        return PARAM_ERR_INVD_PARAM;
    }

    paramWords = pConfig->paramSize / 4;

    PARAM_PRINT("Param init start, size=%lu, sec=%d..%d",
                (unsigned long)pConfig->paramSize, (int)pConfig->secStart, (int)pConfig->secEnd);

    for (s = pConfig->secStart; s >= pConfig->secEnd; s--) {
        uint32_t addr = s * PARAM_SECTOR_SIZE;
        uint32_t sector_end = (s + 1) * PARAM_SECTOR_SIZE;

        while (addr + pConfig->paramSize <= sector_end) {
            headMagic = Internal_ReadWord(addr);

            if (headMagic == pConfig->magicHead) {
                tailMagic = Internal_ReadWord(addr + pConfig->paramSize - 4);
                seq = Internal_ReadWord(addr + pConfig->seqOffset);

                if (tailMagic == pConfig->magicTail) {
                    /* Read entire block to temp buffer for CRC check */
                    uint32_t i;
                    uint32_t *pDest = (uint32_t *)s_scanBuf;

                    for (i = 0; i < paramWords; i++) {
                        pDest[i] = Internal_ReadWord(addr + i * 4);
                    }

                    /* Calculate and verify CRC */
                    calcCrc = CalcParamCRC(s_scanBuf, pConfig->paramSize, pConfig->checksumOffset);
                    storedCrc = GetField32(s_scanBuf, pConfig->checksumOffset);

                    if (calcCrc == storedCrc) {
                        PARAM_PRINT("  Valid block at 0x%08lX: seq=%lu",
                                    (unsigned long)addr, (unsigned long)seq);
                        if (!found || seq > max_seq) {
                            max_seq   = seq;
                            best_addr = addr;
                            best_sec  = (uint16_t)s;
                            found     = true;
                        }
                    } else {
                        PARAM_PRINT("  Block at 0x%08lX CRC mismatch, skip (stored=0x%08lX calc=0x%08lX)",
                                    (unsigned long)addr, (unsigned long)storedCrc, (unsigned long)calcCrc);
                    }
                } else {
                    PARAM_PRINT("  Block at 0x%08lX tail mismatch: 0x%08lX",
                                (unsigned long)addr, (unsigned long)tailMagic);
                }
                addr += pConfig->paramSize;
            } else if (headMagic == 0xFFFFFFFFU) {
                /* Hit empty area, stop scanning this sector */
                break;
            } else {
                addr += 4;
            }
        }
    }

    if (found) {
        /* Load best record into RAM buffer */
        uint32_t *pDest = (uint32_t *)pConfig->pParamBuf;
        uint32_t i;
        for (i = 0; i < paramWords; i++) {
            pDest[i] = Internal_ReadWord(best_addr + i * 4);
        }

        pRuntime->curr_sec  = best_sec;
        pRuntime->curr_addr = best_addr + pConfig->paramSize;   /* 下一空位（非记录本身：
                                    首次保存直接写空位，省掉一次必然失败的覆写重试） */

        PARAM_PRINT("Param load SUCCESS: seq=%lu, addr=0x%08lX, sec=%lu",
                    (unsigned long)max_seq, (unsigned long)best_addr, (unsigned long)best_sec);

        /* 预擦除：剩余不足以再存一次 → 上电满电时刻就地擦除并回写当前记录到扇区头，
           把边界擦除从"掉电保存途中"挪到"上电安静期"。本条件与 Param_Save 溢出条件
           互补：预擦除未触发的上电，之后首次保存必然装得下——正常"一次掉电一存"工况
           下，掉电途中不再触发 8ms 擦除。擦除失败则保留原指针，下次保存走 Param_Save
           原地擦除分支兜底。 */
        if (pRuntime->curr_addr + pConfig->paramSize > (best_sec + 1) * PARAM_SECTOR_SIZE) {
            uint32_t secBase = best_sec * PARAM_SECTOR_SIZE;

            if (Internal_Erase(secBase) == PARAM_OK) {
                SetField32(pConfig->pParamBuf, pConfig->eraseCntOffset,
                           GetField32(pConfig->pParamBuf, pConfig->eraseCntOffset) + 1U);
                SetField32(pConfig->pParamBuf, pConfig->checksumOffset,
                           CalcParamCRC(pConfig->pParamBuf, pConfig->paramSize,
                                        pConfig->checksumOffset));
                (void)Internal_WriteBuffer(secBase, (const uint32_t *)pConfig->pParamBuf,
                                           paramWords);
                pRuntime->curr_addr = secBase + pConfig->paramSize;
                PARAM_PRINT("Param pre-erase: sec=%lu rewritten at head, next=0x%08lX",
                            (unsigned long)best_sec, (unsigned long)pRuntime->curr_addr);
            }
        }
    } else {
        PARAM_PRINT("No valid param block, use defaults and write to Flash");
        pSetDefaults();
        Internal_Erase(pConfig->secStart * PARAM_SECTOR_SIZE);
        pRuntime->curr_sec  = pConfig->secStart;
        pRuntime->curr_addr = pConfig->secStart * PARAM_SECTOR_SIZE;
        SetField32(pConfig->pParamBuf, pConfig->seqOffset, 0);
        SetField32(pConfig->pParamBuf, pConfig->eraseCntOffset, 1);

        /* Save defaults to Flash */
        Param_Save(pConfig, pRuntime);

        PARAM_PRINT("Defaults saved to Flash at addr=0x%08lX", (unsigned long)pRuntime->curr_addr);
    }

    UpdateRuntime(pRuntime, PARAM_OK, pConfig->paramSize);
    return PARAM_OK;
}

/**
 * @brief  Save parameters to Flash (sequential append with wear-leveling).
 */
int32_t Param_Save(const Param_Config_t *pConfig, Param_Runtime_t *pRuntime)
{
    uint8_t  retry = 0;
    uint32_t write_addr;
    uint32_t paramWords;
    uint32_t seq;

    if ((pConfig == NULL) || (pConfig->pParamBuf == NULL) || (pRuntime == NULL)) {
        return PARAM_ERR_INVD_PARAM;
    }

    paramWords = pConfig->paramSize / 4;
    seq = GetField32(pConfig->pParamBuf, pConfig->seqOffset);



    while (retry < MAX_WRITE_RETRY) {
        write_addr = pRuntime->curr_addr;

        /* Check if write would overflow current sector */
        if (write_addr + pConfig->paramSize > (pRuntime->curr_sec + 1) * PARAM_SECTOR_SIZE) {
            uint32_t eraseCnt = GetField32(pConfig->pParamBuf, pConfig->eraseCntOffset);

            if (eraseCnt < MAX_ERASE_LIFE) {
                if (Internal_Erase(pRuntime->curr_sec * PARAM_SECTOR_SIZE) == PARAM_OK) {
                    write_addr = pRuntime->curr_sec * PARAM_SECTOR_SIZE;
                    SetField32(pConfig->pParamBuf, pConfig->eraseCntOffset, eraseCnt + 1);
                } else {
                    retry++;
                    continue;
                }
            } else {
                /* Current sector worn out, move to next sector */
                pRuntime->curr_sec = (pRuntime->curr_sec <= pConfig->secEnd) ?
                                     pConfig->secStart : (pRuntime->curr_sec - 1);
                write_addr = pRuntime->curr_sec * PARAM_SECTOR_SIZE;
                Internal_Erase(pRuntime->curr_sec * PARAM_SECTOR_SIZE);
                SetField32(pConfig->pParamBuf, pConfig->eraseCntOffset, 1);
            }
        }

        /* Update metadata in buffer */
        seq = GetField32(pConfig->pParamBuf, pConfig->seqOffset);
        SetField32(pConfig->pParamBuf, pConfig->seqOffset, seq + 1);
        SetField32(pConfig->pParamBuf, pConfig->checksumOffset,
                   CalcParamCRC(pConfig->pParamBuf, pConfig->paramSize, pConfig->checksumOffset));

        if (Internal_WriteBuffer(write_addr, (const uint32_t *)pConfig->pParamBuf, paramWords) == PARAM_OK) {
            if (Internal_VerifyBuffer(write_addr, (const uint32_t *)pConfig->pParamBuf, paramWords)) {
                /* Write successful — advance position */
                pRuntime->curr_addr = write_addr + pConfig->paramSize;

                PARAM_PRINT("Param save SUCCESS, new_seq=%lu, addr=0x%08lX",
                            (unsigned long)GetField32(pConfig->pParamBuf, pConfig->seqOffset),
                            (unsigned long)write_addr);

                UpdateRuntime(pRuntime, PARAM_OK, pConfig->paramSize);
                return PARAM_OK;
            }
        }

        retry++;
        /* On failure, advance write pointer to skip the bad spot */
        pRuntime->curr_addr = write_addr + pConfig->paramSize;
    }

    UpdateRuntime(pRuntime, PARAM_ERR, pConfig->paramSize);
    PARAM_PRINT("Param save FAILED after %d retries", (int)MAX_WRITE_RETRY);
    return PARAM_ERR;
}

/**
 * @brief  Debug: erase all parameter sectors and re-initialize with defaults.
 */
void Param_Debug_EraseAll(const Param_Config_t *pConfig,
                          Param_Runtime_t *pRuntime,
                          void (*pSetDefaults)(void))
{
    int s;
    PARAM_PRINT("Erase all param sectors start");

    for (s = pConfig->secStart; s >= pConfig->secEnd; s--) {
        Internal_Erase(s * PARAM_SECTOR_SIZE);
    }

    Param_Init(pConfig, pRuntime, pSetDefaults);
    PARAM_PRINT("Erase all param sectors done");
}

/**
 * @brief  Public wrapper: erase a single sector.
 */
int32_t Param_EraseSector(uint32_t address)
{
    return Internal_Erase(address);
}

/*******************************************************************************
 * EOF
 ******************************************************************************/
