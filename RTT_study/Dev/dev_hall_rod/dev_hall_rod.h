/**
 * @file    dev_hall_rod.h
 * @brief   推杆霍尔设备：上下限位霍尔 GPIO 消抖 + 稳定态查询 + 双低故障沿检测
 * @note    B 型执行模型：rod_task 10ms 调 RodHall_Scan()；
 *          双低消抖上升沿由本模块发 EVT_SYS_ROD_LIMIT_FAULT（事件只发跳变沿）。
 */
#ifndef __DEV_HALL_ROD_H__
#define __DEV_HALL_ROD_H__

#include <stdint.h>
#include <stdbool.h>

/* 引脚配置（自 dev_rod_state.h 迁入；上限 PB2 / 下限 PB10，高电平=触发） */
#define ROD_HALL_MAX_PIN    GET_PIN(B, 2)
#define ROD_HALL_MIN_PIN    GET_PIN(B, 10)

/* 消抖窗口：3 点 × 10ms 扫描 = 30ms 稳定判定（仿 dev_polarity 双窗口） */
#define ROD_HALL_WIN_SIZE   (3U)

void RodHall_Init(void);        /* registry init：复位窗口与状态 */
void RodHall_Scan(void);        /* 10ms 扫描：读 GPIO + 推窗 + 稳定判定 + 故障沿 */
bool RodHall_IsAtMax(void);     /* 上限位稳定态 */
bool RodHall_IsAtMin(void);     /* 下限位稳定态 */
bool RodHall_IsFault(void);     /* 双低异常稳定态 */

#endif /* __DEV_HALL_ROD_H__ */
