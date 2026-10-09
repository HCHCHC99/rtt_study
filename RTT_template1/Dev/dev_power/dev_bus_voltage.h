#ifndef __DEV_BUS_VOLTAGE_H__
#define __DEV_BUS_VOLTAGE_H__

#include <stdint.h>

/* 阈值默认值宏（VOL_*_DFT）已集中至 Dev/dev_param/dev_param.h（存储默认值单点） */
#define VOL_OFFSET               (1200.0f) /* 偏置电压 mV：1200mV = 1.2V */

/* ===================== 模拟模式 ===================== */
/* 1=模拟：1ms 检测不读 ADC，改用 g_volt_sim_mv 直接赋值（表达式窗口实时可改）。
   模拟值 = 模块看到的最终电压 mV（ADC 换算与 +1.2V 偏置一并旁路） */
#define VOLT_SIM_MODE_EN        (0)
extern volatile uint32_t g_volt_sim_mv;   /* 模拟母线电压 mV（初值见 .c，可实时改） */

/* 阈值配置（RAM 变量，debugger 可实时改；默认见 .c） */
typedef struct {
    float    over_th;      /* 过压阈值 V */
    float    under_th;     /* 欠压阈值 V */
    float    hyst;         /* 迟滞回差 V */
    uint32_t recover_ms;   /* 恢复延时 ms */
    uint32_t over_ms;      /* 过压确认窗 ms：连续超限 N ms 才判故障（0=单点立即） */
    uint32_t under_ms;     /* 欠压确认窗 ms：连续低于阈值 N ms 才判故障（0=单点立即） */
} VoltCfg_t;
extern volatile VoltCfg_t g_volt_cfg;

void BusVoltage_Init(void);
void BusVoltage_Isr1ms(void); /* 1ms ISR 检测（TMR0_2 心跳调用） */
void BusVoltage_GetInfo(float *pfVolt_V, uint8_t *pu8Status);  /* 0=正常 1=欠压 2=过压 */
uint8_t BusVoltage_IsUnderVoltage(void);  /* 1=当前处于欠压故障（含恢复等待） */
uint8_t BusVoltage_HasSeenValid(void);    /* 本次 MCU 上电后曾见过有效母线电压 */
uint8_t BusVoltage_IsBelowUnderTh(void);  /* 最新采样低于欠压阈值（LED 实时跟踪状态，led 线程兜底轮询用） */
float BusVoltage_GetFaultVolt(void);  /* 故障触发瞬间的电压快照 V（sys_sm 打印用） */

#endif /* __DEV_BUS_VOLTAGE_H__ */








