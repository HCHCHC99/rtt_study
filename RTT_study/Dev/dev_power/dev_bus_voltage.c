/**
 * @file    dev_bus_voltage.c
 * @brief   母线电压设备：读 10ms 滑动均值→过压/欠压（迟滞 + 确认窗 + 恢复延时，1ms ISR 检测）
 */
#include "dev_bus_voltage.h"
#include "dev_param.h"      /* VOL_*_DFT 存储默认值（单点） */
#include "rtt_manager.h"
#include "dev_state.h"
#include "dev_event_def.h"
#include "dev_adc.h"
#include "Task/led_task.h"  /* Led_SetOn：欠压指示灯（1ms 实时跟踪欠压阈值，ISR 上下文可调） */
#include <rtthread.h>


/* 阈值配置全局变量（类型/声明见 dev_bus_voltage.h；debugger 改 g_volt_cfg 实时生效） */
volatile VoltCfg_t g_volt_cfg = {
    VOL_OVER_TH_DFT, VOL_UNDER_TH_DFT, VOL_HYST_DFT, VOL_RECOVER_DELAY_MS_DFT,
    VOL_OVER_MS_DFT, VOL_UNDER_MS_DFT,
};
volatile uint32_t g_volt_sim_mv = 21500U;   /* 模拟母线电压 mV（VOLT_SIM_MODE_EN=1 时生效） */

static volatile float   s_fVolt;       /* 1ms ISR 写，主循环/GetInfo 读 */
static volatile uint8_t s_u8Status;    /* 0 正常 1 欠压 2 过压 */
static volatile uint8_t s_u8SeenValid; /* 本次 MCU 上电后曾见有效母线（IDLE 复位不清） */
static uint8_t s_u8Fault;
static uint8_t s_u8Waiting;
static uint32_t s_u32RecoverCnt;       /* 恢复延时累计 ms（仅 ISR） */
static uint32_t s_u32OverCnt;          /* 过压确认窗连续计数 ms（仅 ISR） */
static uint32_t s_u32UnderCnt;         /* 欠压确认窗连续计数 ms（仅 ISR） */
static volatile float   s_fFaultVolt;  /* 故障触发瞬间的电压快照 V（ISR 写，sys_sm 打印读） */
static volatile uint8_t s_u8BelowTh;   /* 最新采样低于欠压阈值标志（ISR 写，led 线程兜底轮询读） */



void BusVoltage_Init(void)
{
    s_fVolt = 0.0f;
    s_u8Status = 0U;
    s_u8Fault = 0U;
    s_u8Waiting = 0U;
    s_u32RecoverCnt = 0U;
    s_u32OverCnt = 0U;
    s_u32UnderCnt = 0U;
    s_fFaultVolt = 0.0f;
}

/* 1ms ISR 检测（由 Dev_Power_Isr1ms 经 TMR0_2 心跳调用；ISR 内不打印） */
void BusVoltage_Isr1ms(void)
{
    uint8_t u8PrevFault;
    float fVolt = 0.0f;

#if VOLT_SIM_MODE_EN
    fVolt = (float)g_volt_sim_mv * 0.001f;   /* 模拟：直接赋值（ADC 换算与偏置一并旁路） */
#else
    (void)Dev_Adc_GetMean(0U, &fVolt);   /* 10ms 滑动均值，单位 V（已含 150k:10k 分压换算） */
    fVolt += (VOL_OFFSET * 0.001f);      /* 偏置补偿：+1.2V（仅本模块，不影响 ADC 层） */
#endif
    s_fVolt = fVolt;

    /* 欠压指示灯：1ms 实时跟踪欠压阈值——低于亮 / 高于灭（与确认窗、恢复延时无关；
       上电无母线时 0V<阈值 常亮，属预期行为）。同值重写无害（单脚寄存器写）。 */
    s_u8BelowTh = (fVolt < g_volt_cfg.under_th) ? 1U : 0U;
    Led_SetOn(s_u8BelowTh);

    /* 有效母线判据与欠压恢复判据一致：高于欠压阈值 + 迟滞（即欠压已退出）。
       只置 1、不在 IDLE 清零，用于区分"调试无母线冷启动"和"真实掉电"（欠压存行程门控用）。 */
    if (fVolt > (g_volt_cfg.under_th + g_volt_cfg.hyst)) {
        s_u8SeenValid = 1U;
    }

    u8PrevFault = s_u8Fault;

    if (s_u8Fault != 0U) {
        uint8_t u8Recover = (s_u8Fault == 2U) ? (fVolt < (g_volt_cfg.over_th - g_volt_cfg.hyst))
                                              : (fVolt > (g_volt_cfg.under_th + g_volt_cfg.hyst));
        if (u8Recover != 0U) {
            if (s_u8Waiting == 0U) {
                s_u8Waiting = 1U;
                s_u32RecoverCnt = 0U;
                Sys_Event_Send(EVT_SYS_VOLT_RECOVER_WAIT);   /* 通知 sys_sm 打印"电压回到正常区，开始等恢复延时" */
            } else {
                s_u32RecoverCnt++;
                if (s_u32RecoverCnt >= g_volt_cfg.recover_ms) {
                    s_u8Fault = 0U;
                    s_u8Waiting = 0U;
                    s_u32RecoverCnt = 0U;
                    /* LED 不在此控制：由上方 1ms 阈值实时跟踪（回正常区即灭） */
                }
            }
        } else {
            s_u8Waiting = 0U;
            s_u32RecoverCnt = 0U;
        }
    } else {
        if (fVolt > g_volt_cfg.over_th) {
            s_u32UnderCnt = 0U;
            s_u32OverCnt++;
            if (s_u32OverCnt >= g_volt_cfg.over_ms) {   /* 确认窗：连续超限 N ms（0=单点立即） */
                s_u8Fault = 2U;
                s_fFaultVolt = fVolt;      /* 锁存过压触发瞬间的电压值 */
                s_u32OverCnt = 0U;
                s_u32UnderCnt = 0U;
            }
        } else if (fVolt < g_volt_cfg.under_th) {
            s_u32OverCnt = 0U;
            s_u32UnderCnt++;
            if (s_u32UnderCnt >= g_volt_cfg.under_ms) {
                s_u8Fault = 1U;
                s_fFaultVolt = fVolt;      /* 锁存欠压触发瞬间的电压值 */
                s_u32OverCnt = 0U;
                s_u32UnderCnt = 0U;
            }
        } else {
            s_u32OverCnt = 0U;             /* 回到正常区：确认计数清零 */
            s_u32UnderCnt = 0U;
        }
    }
    s_u8Status = s_u8Fault;

    /* 故障/恢复跳变 -> 事件通知系统状态机（恢复发 EVT_SYS_VOLT_NORMAL，由状态机判自动恢复；打印在 sys_sm 线程） */
    if (s_u8Fault != u8PrevFault) {
        if (s_u8Fault == 2U) {
            Sys_Event_Send(EVT_SYS_VOLT_OVER);
        } else if (s_u8Fault == 1U) {
            Sys_Event_Send(EVT_SYS_VOLT_UNDER);
        } else {
            s_fFaultVolt = fVolt;      /* 恢复时刻电压，供 sys_sm 打印恢复值 */
            Sys_Event_Send(EVT_SYS_VOLT_NORMAL);   /* 电压恢复正常（过迟滞+恢复延时） */
        }
    }
}

void BusVoltage_GetInfo(float *pfVolt_V, uint8_t *pu8Status)
{
    if (pfVolt_V != NULL)  *pfVolt_V = s_fVolt;
    if (pu8Status != NULL) *pu8Status = s_u8Status;
}

uint8_t BusVoltage_IsUnderVoltage(void)
{
    return (s_u8Status == 1U) ? 1U : 0U;
}

uint8_t BusVoltage_HasSeenValid(void)
{
    return (s_u8SeenValid != 0U) ? 1U : 0U;
}

uint8_t BusVoltage_IsBelowUnderTh(void)
{
    return (s_u8BelowTh != 0U) ? 1U : 0U;
}

float BusVoltage_GetFaultVolt(void)
{
    return s_fFaultVolt;
}

/* EOF */













