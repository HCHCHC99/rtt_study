/**
 * @file    dev_power_isr.c
 * @brief   电源设备 1ms ISR 分发
 * @note    注册到 HcDrv_Timer_Start1ms()；ISR 上下文：不做打印/浮点除法以外的重活，
 *          过压/欠压/过流事件经 rt_event_send（ISR 安全）通知状态机，打印在 sys_sm 线程。
 */
#include "dev_power_isr.h"
#include "dev_bus_voltage.h"
#include "dev_cur_sensor.h"

void Dev_Power_Isr1ms(void)
{
    BusVoltage_Isr1ms();
    CurrentSensor_Isr1ms();
}

/* EOF */


