/**
 * @file    di_task.h
 * @brief   DI 采集任务：10ms 周期驱动数字输入设备扫描（电源极性 + 未来限位）
 * @note    事件逻辑在设备内（dev_polarity 等），本任务只做"定时调用设备 Scan"。
 */
#ifndef __DI_TASK_H__
#define __DI_TASK_H__

#include "task_set.h"     /* 栈大小/优先级统一管理 */

/* 默认配置宏（统一放头文件） */
#define DI_SCAN_PERIOD_MS    (2U)     /* DI 采样周期 ms：2ms */
#define DI_THREAD_STACK      TASK_STACK_DI   /* 引用 task_set.h 唯一来源 */
#define DI_THREAD_PRIO       TASK_PRIO_DI     /* 中优先级，早于低优先级的 LED/监控 */
#define DI_THREAD_TICK       (10)

/* 初始化并启动 DI 采集线程 */
void Di_Task_Start(void);

#endif /* __DI_TASK_H__ */
