/**
 * @file    hc32_drv_flash.c
 * @brief   HC32F460 片内 Flash 读写擦薄封装（EFM）
 * @note    移植自裸机工程 hc32f46x_flash.c（接口与解锁流程保持一致）。
 */
#include "hc32_drv_flash.h"

/* EFM 状态映射到封装状态 */
static HC32FLASH_STATUS MapEFMStatusToHC32(int32_t efmStatus)
{
    switch (efmStatus)
    {
        case LL_OK:
            return HC32FLASH_OK;
        case LL_ERR_NOT_RDY:
            return HC32FLASH_BUSY;
        case LL_ERR_TIMEOUT:
            return HC32FLASH_BUSY;
        default:
        {
            if (SET == EFM_GetStatus(EFM_FLAG_PEPRTERR))
                return HC32FLASH_WPRERR;
            if (SET == EFM_GetStatus(EFM_FLAG_PGSZERR))
                return HC32FLASH_PGAERR;
            if (SET == EFM_GetStatus(EFM_FLAG_PEWERR))
                return HC32FLASH_PEWERR;
            if (SET == EFM_GetStatus(EFM_FLAG_COLERR))
                return HC32FLASH_COLERR;
            if (SET == EFM_GetStatus(EFM_FLAG_PGMISMTCH))
                return HC32FLASH_PGMISMTCH;
            return HC32FLASH_OK;
        }
    }
}

/* 获取 Flash 状态（调试用） */
HC32FLASH_STATUS HC32FLASH_GetStatus(void)
{
    HC32FLASH_STATUS status = HC32FLASH_OK;
    uint32_t fsr_value;

    fsr_value = CM_EFM->FSR;
    (void)fsr_value;   /* 调试观测用；打印关闭时抑制未使用告警 */

    if (RESET == EFM_GetStatus(EFM_FLAG_RDY))
    {
        status = HC32FLASH_BUSY;
        FLASH_PRINT("FLASH status: BUSY, FSR=0x%08lX", (unsigned long)fsr_value);
    }
    else if (SET == EFM_GetStatus(EFM_FLAG_PEPRTERR))
    {
        status = HC32FLASH_WPRERR;
        FLASH_PRINT("FLASH status: WPRERR, FSR=0x%08lX", (unsigned long)fsr_value);
    }
    else if (SET == EFM_GetStatus(EFM_FLAG_PGSZERR))
    {
        status = HC32FLASH_PGAERR;
        FLASH_PRINT("FLASH status: PGAERR, FSR=0x%08lX", (unsigned long)fsr_value);
    }
    else if (SET == EFM_GetStatus(EFM_FLAG_PEWERR))
    {
        status = HC32FLASH_PEWERR;
        FLASH_PRINT("FLASH status: PEWERR, FSR=0x%08lX", (unsigned long)fsr_value);
    }
    else if (SET == EFM_GetStatus(EFM_FLAG_COLERR))
    {
        status = HC32FLASH_COLERR;
        FLASH_PRINT("FLASH status: COLERR, FSR=0x%08lX", (unsigned long)fsr_value);
    }
    else if (SET == EFM_GetStatus(EFM_FLAG_PGMISMTCH))
    {
        status = HC32FLASH_PGMISMTCH;
        FLASH_PRINT("FLASH status: PGMISMTCH, FSR=0x%08lX", (unsigned long)fsr_value);
    }

    return status;
}

/* 解锁 Flash 寄存器 */
static void HC32FLASH_Unlock(void)
{
    EFM_REG_Unlock();                   /* 解除 EFM 寄存器写保护 */
    EFM_FWMC_Cmd(ENABLE);               /* 设定擦写模式许可 */
    EFM_SetBusStatus(EFM_BUS_HOLD);     /* 忙时总线停等（取指停等，保证正确性） */
}

/* 上锁 Flash 寄存器 */
static void HC32FLASH_Lock(void)
{
    EFM_FWMC_Cmd(DISABLE);              /* 取消擦写模式许可 */
    EFM_REG_Lock();                     /* 恢复写保护 */
}

/* 扇区擦除 */
HC32FLASH_STATUS HC32FLASH_EraseSector(uint32_t u32Addr)
{
    int32_t efmRet;
    HC32FLASH_STATUS status;

    FLASH_PRINT("Erase sector start, addr=0x%08lX", (unsigned long)u32Addr);

    HC32FLASH_Unlock();                 /* 1. 解锁 */
    EFM_ClearStatus(EFM_FLAG_ALL);      /* 2. 清除擦写错误标志 */
    efmRet = EFM_SectorErase(u32Addr);  /* 3. 扇区擦除 */
    status = MapEFMStatusToHC32(efmRet);
    EFM_ClearStatus(EFM_FLAG_ALL);      /* 4. 清除标志位 */
    HC32FLASH_Lock();                   /* 5. 退出擦写模式并上锁 */

    if (status == HC32FLASH_OK)
    {
        FLASH_PRINT("Erase sector success, addr=0x%08lX", (unsigned long)u32Addr);
    }
    else
    {
        FLASH_PRINT("Erase sector failed, addr=0x%08lX, status=%d",
                    (unsigned long)u32Addr, (int)status);
    }

    return status;
}

/* 无回读校验的单字写入 */
HC32FLASH_STATUS HC32FLASH_WritedWord_NoCheck(uint32_t u32Addr, uint32_t data)
{
    int32_t efmRet;
    HC32FLASH_STATUS status;

    FLASH_PRINT("Write word no check start, addr=0x%08lX, data=0x%08lX",
                (unsigned long)u32Addr, (unsigned long)data);

    HC32FLASH_Unlock();                 /* 解锁 */
    EFM_ClearStatus(EFM_FLAG_ALL);      /* 清除擦写错误标志 */

    efmRet = EFM_ProgramWord(u32Addr, data);
    status = MapEFMStatusToHC32(efmRet);

    EFM_ClearStatus(EFM_FLAG_ALL);      /* 清除擦写错误标志 */
    HC32FLASH_Lock();                   /* 上锁 */

    if (status == HC32FLASH_OK)
    {
        FLASH_PRINT("Write word no check success, addr=0x%08lX, data=0x%08lX",
                    (unsigned long)u32Addr, (unsigned long)data);
    }
    else
    {
        FLASH_PRINT("Write word no check failed, addr=0x%08lX, data=0x%08lX, status=%d",
                    (unsigned long)u32Addr, (unsigned long)data, (int)status);
    }

    return status;
}

/* 带回读校验的单字写入 */
HC32FLASH_STATUS HC32FLASH_WritedWord_Check(uint32_t u32Addr, uint32_t data)
{
    int32_t efmRet;
    HC32FLASH_STATUS status;
    uint32_t read_back_data;

    FLASH_PRINT("Write word with check start, addr=0x%08lX, data=0x%08lX",
                (unsigned long)u32Addr, (unsigned long)data);

    HC32FLASH_Unlock();                 /* 解锁 */
    EFM_ClearStatus(EFM_FLAG_ALL);      /* 清除擦写错误标志 */

    efmRet = EFM_ProgramWordReadBack(u32Addr, data);
    status = MapEFMStatusToHC32(efmRet);

    /* 状态显示成功后再做一次手动回读校验 */
    if (status == HC32FLASH_OK)
    {
        read_back_data = HC32FLASH_ReaddWord(u32Addr);
        if (read_back_data != data)
        {
            status = HC32FLASH_PGMISMTCH;
            FLASH_PRINT("Write word verify failed, addr=0x%08lX, expect=0x%08lX, actual=0x%08lX",
                        (unsigned long)u32Addr, (unsigned long)data, (unsigned long)read_back_data);
        }
    }

    EFM_ClearStatus(EFM_FLAG_ALL);      /* 清除擦写错误标志 */
    HC32FLASH_Lock();                   /* 上锁 */

    if (status == HC32FLASH_OK)
    {
        FLASH_PRINT("Write word with check success, addr=0x%08lX, data=0x%08lX",
                    (unsigned long)u32Addr, (unsigned long)data);
    }

    return status;
}

/* 读取一个字 */
uint32_t HC32FLASH_ReaddWord(uint32_t u32Addr)
{
    return *(volatile uint32_t *)u32Addr;
}

/* EOF */
