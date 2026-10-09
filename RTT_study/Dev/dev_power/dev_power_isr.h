/**
 * @file    dev_power_isr.h
 * @brief   电源设备 1ms ISR 分发（由 Adp/hc_drv_timer 的 TMR0_2 心跳调用）
 */
#ifndef __DEV_POWER_ISR_H__
#define __DEV_POWER_ISR_H__

/* 1ms 检测心跳：读 ADC 10ms 滑动均值 -> 电流/电压阈值判定 -> 事件通知状态机 */
void Dev_Power_Isr1ms(void);

#endif /* __DEV_POWER_ISR_H__ */
