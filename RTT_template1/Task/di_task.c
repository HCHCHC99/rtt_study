/**
 * @file    di_task.c
 * @brief   DI 采集任务实现（2ms 周期）
 * @note    执行模型：B→C（独立线程）；只调设备 Scan，不含业务判定/事件逻辑；
 *          电源极性消抖窗口 5 点 x 2ms 采样 = 10ms。
 */
#include "di_task.h"
#include "dev_polarity.h"
#include <rtthread.h>


static void di_thread_entry(void *param)
{
    (void)param;
    while (1) {
        Polarity_Scan();              /* 设备扫描：内部读 GPIO + 窗口判定 + 跳变发事件 */
        /* 未来: Limit_Scan(); */
        Task_Set_Beat();
        rt_thread_mdelay(DI_SCAN_PERIOD_MS);
    }
}

void Di_Task_Start(void)
{
    (void)Task_Set_Create("di", di_thread_entry, RT_NULL,
                          DI_THREAD_STACK, DI_THREAD_PRIO, 50U);
}

/* EOF */

