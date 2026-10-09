/**
 * @file    dev_monitor.h
 * @brief   系统观测模块：全局 g_monitor，debugger Watch 实时查看（RTT 打印可选）
 * @note    状态字段用枚举类型，Watch 直接显示名称；Monitor_Task 周期性刷新（registry B 模式）。
 */
#ifndef __DEV_MONITOR_H__
#define __DEV_MONITOR_H__

#include "dev_state.h"      /* SysState_t */
#include "dev_bus_voltage.h" /* VoltCfg_t / g_volt_cfg */
#include "dev_cur_sensor.h"  /* CurCfg_t / g_cur_cfg */
#include "dev_polarity.h" /* PolarityState_t */
#include <stdint.h>

/* 电压/电流状态枚举（便于 Watch 显示名称） */
typedef enum { VOLT_NORMAL = 0, VOLT_UNDER, VOLT_OVER } MonitorVoltSt_t;
typedef enum { CUR_NORMAL = 0, CUR_OVER }                MonitorCurSt_t;

typedef struct {
    /* 系统 */
    SysState_t      sys_state;        /* 系统状态：枚举，Watch 显示 RUN/FAULT 等 */
    uint32_t        error_code;       /* 故障码 0=NONE 1=OC 2=OV 3=UV */
    uint32_t        sys_events;       /* 系统事件组当前值 */
    uint32_t        uptime_ms;        /* 运行时间 */

    /* ADC */
    uint16_t        adc_raw_v, adc_raw_i;   /* 最新原始 AD */
    float           adc_mean_v, adc_mean_i; /* 10ms 滑动均值，均单位 V（adc_mean_i 为电流通道电压，mA 见 cur_ma） */

    /* 母线电压 */
    float           bus_volt;         /* 滤波+偏置后 V */
    MonitorVoltSt_t volt_status;      /* 枚举：NORMAL/UNDER/OVER */

    /* 电流传感器 */
    float           cur_ma;           /* 10ms 均值 mA */
    MonitorCurSt_t  cur_status;       /* 枚举：NORMAL/OVER */
    uint16_t        cur_over_ms;      /* 超阈值累计 ms */

    /* 电源极性 */
    PolarityState_t polarity;         /* 枚举：UNKNOWN/UNPOWERED/FWD/REV/ABNORMAL */

    /* 推杆（axis0 观测） */
    uint8_t  rod_state;         /* RodState_t */
    float    rod_pos_mm;        /* 当前位置 mm */
    uint8_t  rod_calibrated;    /* 是否已校准 */

    /* 阈值配置：显示镜像 + 指针（Watch 改 volt_cfg/cur_cfg 字段实时生效） */
    float    volt_over_th;
    float    volt_under_th;
    float    volt_hyst;
    uint32_t volt_recover_ms;
    float    cur_over_th;
    uint16_t cur_over_window;
    volatile VoltCfg_t *volt_cfg;   /* 指向 g_volt_cfg */
    volatile CurCfg_t  *cur_cfg;    /* 指向 g_cur_cfg */

    /* 任务 */
    uint8_t         di_running;
    uint16_t        di_period_ms;
    uint8_t         led_running;
    uint8_t         led_state;
    uint16_t        led_period_ms;
} Monitor_t;

/* 全局观测变量：debugger Watch 直接添加 g_monitor */
extern volatile Monitor_t g_monitor;

void Monitor_Init(void);
void Monitor_Task(void);            /* 刷新 g_monitor（registry B 模式周期调用） */
void Monitor_DumpAll(void);         /* 可选：RTT 打印 g_monitor 全部 */
void Monitor_DumpSys(void);
void Monitor_DumpStatus(void);        /* 1s 打印：系统状态机 + 过压/欠压/过流状态 */
void Monitor_DumpDev(void);
void Monitor_DumpTask(void);

/* 状态枚举 → 名字（诊断打印用；越界返回 UNK） */
const char *Monitor_SysStateName(uint8_t state);

#endif /* __DEV_MONITOR_H__ */

