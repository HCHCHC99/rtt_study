/**
 * @file    hc32_drv_flash.h
 * @brief   HC32F460 片内 Flash 读写擦薄封装（EFM）
 * @note    移植自裸机工程 hc32f46x_flash.c/.h；
 *          - 解锁流程：EFM_REG_Unlock → EFM_FWMC_Cmd(ENABLE) → EFM_SetBusStatus(BUS_HOLD)，
 *            操作完 EFM_FWMC_Cmd(DISABLE) → EFM_REG_Lock，调用方无需关心；
 *          - 擦/写期间 EFM_BUSY 时 CPU 取指停等（单 bank），全系统短暂停顿，勿在电机运行时调用；
 *          - 不带 .ramfunc：BUS_HOLD 模式下停等即可保证正确性，Keil scatter 无独立 ramfunc 执行域。
 */
#ifndef __HC32_DRV_FLASH_H__
#define __HC32_DRV_FLASH_H__

#include <stdint.h>
#include "hc32_ll_efm.h"

/* 打印开关：rtt_manager.h 的 FLASH_PRINT（默认关） */
#include "rtt_manager.h"

/* FLASH 操作状态 */
typedef enum
{
    HC32FLASH_OK = 0,           /* 操作完成 */
    HC32FLASH_BUSY = 1,         /* 忙 */
    HC32FLASH_COLERR = 2,       /* 读写访问错误 */
    HC32FLASH_PGMISMTCH = 3,    /* 单编程回读错误 */
    HC32FLASH_PGAERR = 4,       /* 编程对齐错误 */
    HC32FLASH_WPRERR = 5,       /* 写保护错误 */
    HC32FLASH_PEWERR = 6,       /* 在擦写不许可模式下擦写FLASH */
} HC32FLASH_STATUS;

/* 函数声明 */
HC32FLASH_STATUS HC32FLASH_GetStatus(void);
HC32FLASH_STATUS HC32FLASH_EraseSector(uint32_t u32Addr);
HC32FLASH_STATUS HC32FLASH_WritedWord_NoCheck(uint32_t u32Addr, uint32_t data);
HC32FLASH_STATUS HC32FLASH_WritedWord_Check(uint32_t u32Addr, uint32_t data);
uint32_t HC32FLASH_ReaddWord(uint32_t u32Addr);

#endif /* __HC32_DRV_FLASH_H__ */
