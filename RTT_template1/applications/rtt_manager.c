/**
 * @file    rtt_manager.c
 * @brief   RTT 打印开关管理（开关状态查询）
 */
#include "rtt_manager.h"

void RttManager_DumpSwitches(void)
{
    MAIN_D("[RTT_MGR] SYS_STATE=%d DEV_REG=%d POWER=%d QUEUE_INIT=%d",
           SYS_STATE_PRINT_EN, DEV_REG_PRINT_EN, POWER_PRINT_EN, QUEUE_INIT_PRINT_EN);
}

/* EOF */
