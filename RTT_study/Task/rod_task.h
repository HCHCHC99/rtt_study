/**
 * @file    rod_task.h
 * @brief   推杆位置/状态 10ms 周期更新任务（霍尔→位置→限位→状态）
 */
#ifndef __ROD_TASK_H__
#define __ROD_TASK_H__

#include "task_set.h"     /* 栈大小/优先级统一管理 */

/* 默认配置宏（统一放头文件） */
#define ROD_SCAN_PERIOD_MS    (10U)     /* 推杆位置/状态更新周期 ms */
#define ROD_THREAD_STACK      TASK_STACK_ROD   /* 引用 task_set.h 唯一来源 */
#define ROD_THREAD_PRIO       TASK_PRIO_ROD      /* 中高优先级，早于 DI/LED/监控 */
#define ROD_THREAD_TICK       (10)

/* 初始化并启动推杆更新线程 */
void Rod_Task_Start(void);

#endif /* __ROD_TASK_H__ */
