/**
 * @file    dev_monitor.c
 * @brief   系统观测模块实现：周期性刷新 g_monitor（registry B 模式，100ms）
 * @note    Watch 直接看 g_monitor；RTT 打印为可选辅助（MONITOR_PRINT）。
 */
#include "dev_monitor.h"
#include "dev_model.h"     /* mySystem */
#include "dev_adc.h"
#include "dev_bus_voltage.h"
#include "dev_cur_sensor.h"
#include "dev_rod_position.h"
#include "hc32_drv_gpio.h"         /* Hc32_Gpio_Read / PH2 */
#include "rtt_manager.h"
#include <rtthread.h>
#include <string.h>

/* 全局观测变量 */
volatile Monitor_t g_monitor = {0};

/* 枚举名表（仅 RTT 打印用；Watch 靠枚举类型自动显示名称） */
static const char *const s_sys_state_name[] = {
    "INIT", "IDLE", "RUN", "FAULT", "EMERGENCY", "STOP",
};
static const char *const s_err_name[] = { "NONE", "OC", "OV", "UV", "RL" };
static const char *const s_volt_st_name[] = { "NORMAL", "UNDER", "OVER" };
static const char *const s_cur_st_name[] = { "NORMAL", "OVER" };
static const char *const s_pol_name[] = {
    "UNKNOWN", "UNPOWERED", "FWD", "REV", "ABNORMAL",
};
static const char *const s_rod_calib_name[] = { "NOT_CAL", "CALIB" };

static char s_rod_pos_a[12];    /* Monitor_DegFmt 输出缓冲 */

/* float 度 → "[符号]整数.1位" 字符串（打印规范：不打印浮点；0.1° 精度 >> 脉冲当量 0.025°） */
static void Monitor_DegFmt(float v, char *pBuf, uint32_t bufSize)
{
    uint8_t neg = (v < 0.0f) ? 1U : 0U;
    uint32_t x10;

    if (neg) {
        v = -v;
    }
    x10 = (uint32_t)(v * 10.0f + 0.5f);
    rt_snprintf(pBuf, bufSize, "%s%lu.%01lu", (neg ? "-" : ""),
                (unsigned long)(x10 / 10U), (unsigned long)(x10 % 10U));
}

/* 状态枚举 → 名字（诊断打印用；越界返回 UNK） */
const char *Monitor_SysStateName(uint8_t state)
{
    if (state >= (uint8_t)(sizeof(s_sys_state_name) / sizeof(s_sys_state_name[0]))) {
        return "UNK";
    }
    return s_sys_state_name[state];
}

void Monitor_Init(void)
{
    memset((void *)&g_monitor, 0, sizeof(g_monitor));
    g_monitor.sys_state    = (SysState_t)SYS_STATE_INIT;
    g_monitor.volt_status  = VOLT_NORMAL;
    g_monitor.cur_status   = CUR_NORMAL;
    g_monitor.polarity     = POLARITY_UNKNOWN;
}

/* 周期性刷新 g_monitor（registry B 模式调用；只读模块接口，不做打印/阻塞）
 * 注：g_monitor 为 volatile，API 调用走局部变量再写回，避免 volatile 指针传参警告 */
void Monitor_Task(void)
{
    uint16_t raw_v = 0U, raw_i = 0U;
    float    mean_v = 0.0f, mean_i = 0.0f, volt = 0.0f, cur = 0.0f;
    uint8_t  st;

    /* 系统 */
    g_monitor.sys_state  = (SysState_t)StateMachine_GetState(&mySystem.sys_sm);
    g_monitor.error_code = mySystem.error_code;
    g_monitor.sys_events = (mySystem.sys_evt != RT_NULL) ? mySystem.sys_evt->set : 0U;
    g_monitor.uptime_ms  = rt_tick_get();

    /* ADC */
    (void)Dev_Adc_GetRaw(0, &raw_v);
    (void)Dev_Adc_GetRaw(1, &raw_i);
    (void)Dev_Adc_GetMean(0, &mean_v);
    (void)Dev_Adc_GetMean(1, &mean_i);   /* CH6 ADC 层只出电压 V（mA 换算在 dev_cur_sensor，见 cur_ma） */
    g_monitor.adc_raw_v   = raw_v;
    g_monitor.adc_raw_i   = raw_i;
    g_monitor.adc_mean_v  = mean_v;       /* V */
    g_monitor.adc_mean_i  = mean_i;       /* 电流通道 ADC 电压 V（非 mA；mA 见 cur_ma） */

    /* 母线电压 */
    st = 0U;
    BusVoltage_GetInfo(&volt, &st);
    g_monitor.bus_volt    = volt;
    g_monitor.volt_status = (MonitorVoltSt_t)st;

    /* 电流传感器 */
    st = 0U;
    CurrentSensor_GetInfo(&cur, &st);
    g_monitor.cur_ma      = cur;
    g_monitor.cur_status  = (MonitorCurSt_t)st;
    g_monitor.cur_over_ms = CurrentSensor_GetOverMs();

    /* 电源极性 */
        g_monitor.polarity = Polarity_GetState();

    /* 推杆（axis0） */
    if (mySystem.axis[0].dir != ACT_DIR_NONE) {
        g_monitor.rod_state      = (uint8_t)StateMachine_GetState(&mySystem.axis[0].sm_act);
        g_monitor.rod_pos_mm     = RodPosition_GetCurrent(&mySystem.axis[0].position);
        g_monitor.rod_calibrated = (uint8_t)RodPosition_IsCalibrated(&mySystem.axis[0].position);
    } else {
        g_monitor.rod_state = 0U;
        g_monitor.rod_pos_mm = 0.0f;
        g_monitor.rod_calibrated = 0U;
    }

    /* 阈值配置：指针 + 显示镜像（改 g_volt_cfg/g_cur_cfg 实时生效） */
    g_monitor.volt_cfg        = &g_volt_cfg;
    g_monitor.cur_cfg         = &g_cur_cfg;
    g_monitor.volt_over_th    = g_volt_cfg.over_th;
    g_monitor.volt_under_th   = g_volt_cfg.under_th;
    g_monitor.volt_hyst       = g_volt_cfg.hyst;
    g_monitor.volt_recover_ms = g_volt_cfg.recover_ms;
    g_monitor.cur_over_th     = g_cur_cfg.over_th_ma;
    g_monitor.cur_over_window = g_cur_cfg.window_ms;

    /* 任务状态（周期为编译期常量；led 读实际电平） */
    g_monitor.di_running    = 1U;
    g_monitor.di_period_ms  = 2U;
    g_monitor.led_running   = 1U;
    g_monitor.led_state     = Hc32_Gpio_Read(PH2_PORT, PH2_PIN);
    g_monitor.led_period_ms = 1000U;
}

/* ============ 可选 RTT 打印 ============ */
/* 1s 打印：系统状态机 + 是否过压/欠压/过流（main 1s 循环调用） */
void Monitor_DumpStatus(void)
{
    MONITOR_SYS_PRINT("=====================================");
    MONITOR_SYS_PRINT("sm: sys=%s err=%s",
                  s_sys_state_name[(uint8_t)g_monitor.sys_state],
                  s_err_name[g_monitor.error_code & 0x7U]);
    MONITOR_SYS_PRINT("fault: volt=%s cur=%s",
                  s_volt_st_name[g_monitor.volt_status],
                  s_cur_st_name[g_monitor.cur_status]);
    /* 推杆位置/校准（转动模式：单位=度；数据由 Monitor_Task 100ms 刷新） */
    if (g_monitor.rod_calibrated < 2U) {
        Monitor_DegFmt(g_monitor.rod_pos_mm, s_rod_pos_a, sizeof(s_rod_pos_a));
        ROD_POS_PRINT("calib=%s pos=%sdeg",
                      s_rod_calib_name[g_monitor.rod_calibrated], s_rod_pos_a);
    } else {
        ROD_POS_PRINT("calib=UNK");
    }
    MONITOR_SYS_PRINT("=====================================");
}

void Monitor_DumpSys(void)
{
    uint8_t st  = (uint8_t)g_monitor.sys_state;
    uint8_t err = (uint8_t)(g_monitor.error_code & 0x3U);
    MONITOR_PRINT("sys=%s err=%s evt=0x%lx uptime=%lums",
                  s_sys_state_name[st], s_err_name[err],
                  (unsigned long)g_monitor.sys_events,
                  (unsigned long)g_monitor.uptime_ms);
}

void Monitor_DumpDev(void)
{
    MONITOR_PRINT("adc raw=%u/%u mean=%lummV/%lummV",
                  g_monitor.adc_raw_v, g_monitor.adc_raw_i,
                  (unsigned long)(g_monitor.adc_mean_v * 1000.0f),
                  (unsigned long)(g_monitor.adc_mean_i * 1000.0f));
    MONITOR_PRINT("volt=%lummV %s cur=%lumA %s over_ms=%ums pol=%s",
                  (unsigned long)(g_monitor.bus_volt * 1000.0f),
                  s_volt_st_name[g_monitor.volt_status],
                  (unsigned long)g_monitor.cur_ma,
                  s_cur_st_name[g_monitor.cur_status],
                  g_monitor.cur_over_ms,
                  s_pol_name[g_monitor.polarity]);
}

void Monitor_DumpTask(void)
{
    MONITOR_PRINT("di=%u %ums led=%u %ums gpio=%u",
                  g_monitor.di_running, g_monitor.di_period_ms,
                  g_monitor.led_running, g_monitor.led_period_ms,
                  g_monitor.led_state);
}

void Monitor_DumpAll(void)
{
    Monitor_DumpSys();
    Monitor_DumpDev();
    Monitor_DumpTask();
}

/* EOF */




