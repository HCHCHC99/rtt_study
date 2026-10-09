/**
 * @file    hc_drv_timer.h
 * @brief   TMR6 微秒级时间基准（时基计数 + 全局 us 时间戳）
 * @note    HC32 TMR6 底层驱动（实现 Utils/us_timer 接口）；TMR6_2，PCLK0/64
 */
#ifndef __HC_DRV_TIMER_H__
#define __HC_DRV_TIMER_H__

#include "hc32_ll.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 / 启停 */
void Timer6_Timebase_Init(void);
void Timer6_Timebase_Start(void);
void Timer6_Timebase_Stop(void);

/* 计数 / 差值 */
uint32_t Timer6_Timebase_GetCounter(void);
uint32_t Timer6_Timebase_GetDelta(void);          /* 距上次 GetDelta 的计数值差（处理回绕） */
uint32_t Timer6_Timebase_GetPulseInterval(void);  /* 距上次的间隔，单位 us */

/* 频率 / 换算 */
uint32_t Timer6_Timebase_GetFrequency(void);      /* 实际计数频率 Hz */
uint32_t Timer6_Timebase_DeltaToUs(uint32_t u32DeltaCounter);
uint32_t Timer6_Timebase_DeltaToMs(uint32_t u32DeltaCounter);

/* 脉冲时间 / RPM */
uint32_t Timer6_Timebase_GetPulseTime(uint8_t pulse_count);
float Timer6_Timebase_CalculateRPM(uint32_t time_us, uint8_t pulses, uint8_t pulses_per_rev);

/* 全局微秒时间戳（先 UpdateTimestamp 再 GetTimestamp） */
void    Timer6_Timebase_UpdateTimestamp(void);
uint64_t Timer6_Timebase_GetTimestamp(void);

/* 打印所有时钟频率（MAIN_D，英文整型；时钟设好后调用） */
void    Print_All_Clock_Freq(void);

/* 1ms 检测心跳（TMR0_2 CH_A，CMP 中断）：注册回调，ISR 里每 1ms 调用一次（如 Dev_Power_Isr1ms） */
int     HcDrv_Timer_Start1ms(void (*pfnCb)(void));

#ifdef __cplusplus
}
#endif

/* 通用 us 计时器接口表实现（供 Utils/us_timer 绑定） */
struct us_timer_ops;
extern const struct us_timer_ops hc_us_timer_ops;


/* 配置宏（统一放头文件） */
#define TMR6_DIV_VAL          64u     /* TMR6 定时器分频值 */
#define TMR6_PERIOD_MAX       0xFFFFu /* 16 位定时器最大值 */

/* 1ms 检测心跳（TMR0_2 CH_A）配置 */
#define TMR0_1MS_UNIT       (CM_TMR0_2)
#define TMR0_1MS_CH         (TMR0_CH_A)
#define TMR0_1MS_CLK        (FCG2_PERIPH_TMR0_2)
#define TMR0_1MS_CLK_DIV    (TMR0_CLK_DIV64)
#define TMR0_1MS_DIV_VAL    (64UL)
#define TMR0_1MS_IRQ_SRC    (INT_SRC_TMR0_2_CMP_A)
#define TMR0_1MS_IRQn       (INT007_IRQn)
#define TMR0_1MS_INT_PRIO   (DDL_IRQ_PRIO_03)
#endif /* __HC_DRV_TIMER_H__ */




