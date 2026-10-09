/**
 * @file    led_task.h
 * @brief   LED 任务：欠压指示灯。电压检测 ISR 每 1ms 实时直控（低于欠压阈值亮/
 *          高于灭，上电无母线时 0V<阈值 常亮属预期）；led 线程 1s 轮询兜底同步。
 *          LED 极性：亮 = 置低，灭 = 置高。
 */
#ifndef __LED_TASK_H__
#define __LED_TASK_H__

#include <stdint.h>
#include "task_set.h"     /* 栈大小/优先级统一管理 */

/* 默认配置宏（统一放头文件） */
#define LED_POLL_MS      (1000U)  /* led 线程欠压状态轮询周期 ms（兜底，实时点亮在欠压 ISR） */
#define LED_PORT        (PH2_PORT)      /* GPIO_PORT_H */
#define LED_PIN         (PH2_PIN)       /* GPIO_PIN_02 */
#define LED_THREAD_STACK    TASK_STACK_LED   /* 引用 task_set.h 唯一来源 */
#define LED_THREAD_PRIO     TASK_PRIO_LED        /* 低优先级，不影响电源/控制 */
#define LED_THREAD_TICK     (10)

/* 初始化 LED GPIO（上电常灭）并启动轮询线程 */
void Led_Task_Start(void);

/* LED 亮灭控制（ISR/线程上下文均可调，单脚写原子）：on=1 亮(置低) / on=0 灭(置高) */
void Led_SetOn(uint8_t on);

#endif /* __LED_TASK_H__ */
