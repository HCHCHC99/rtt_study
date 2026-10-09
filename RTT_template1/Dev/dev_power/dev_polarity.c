/**
 * @file    dev_polarity.c
 * @brief   电源极性设备实现（单管方向检测 + 滑窗消抖 + 状态跳变事件）
 * @note    由 Task/di_task（2ms 线程）调用，不做打印/阻塞；
 *          PB15 单管+上拉：低=FWD 高=REV（真值见 dev_polarity.h）；
 *          跳变沿发轴事件（Act_Event_Send，ISR 安全）+ 系统事件 EVT_SYS_POLARITY_CHG
 *          （方向相关故障清除的决策在 dev_state，本模块不感知故障语义）；
 *          仲裁命令 RUN_FWD/REV @98% 经 Polarity_SendArb（队列满只计数告警）。
 */
#include "dev_polarity.h"
#include "dev_act.h"
#include "dev_model.h"
#include "dev_state.h"     /* Sys_Event_Send：方向沿 -> EVT_SYS_POLARITY_CHG */
#include "dev_event_def.h"
#include "dev_bus_voltage.h"  /* 母线无效/欠压期间，PB15 上拉不是有效方向 */
#include "drv_gpio.h"          /* GET_PIN / GPIO_PORT_B */
#include "rtt_manager.h"
#include "us_timer.h"
#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>

/* ============ 窗口参数 ============ */

/* 单窗口：位掩码（bit0 最新）+ 已填点数 */
typedef struct {
    uint16_t win;
    uint8_t  cnt;
} PolarityWin_t;

/* ============ 本地状态 ============ */
static volatile PolarityState_t s_state = POLARITY_UNKNOWN;
static PolarityWin_t s_dirWin;
static uint8_t s_bInit = 0U;
static volatile uint8_t s_u8PendingState = 0U;  /* 待打印的跳变状态（ISR 置位，线程清） */
static volatile uint32_t s_arb_send_fail_count = 0U;
volatile PolarityState_t g_pol_sim_state = POLARITY_UNKNOWN;   /* 模拟极性（POLARITY_SIM_MODE_EN=1 时生效） */

/* ============ 窗口操作 ============ */
static uint8_t Polarity_WinFull(const PolarityWin_t *w)
{
    return (w->cnt >= POLARITY_WIN_SIZE);
}

/* 推入一个采样点，环形覆盖最旧位 */
static void Polarity_WinPush(PolarityWin_t *w, uint8_t bit)
{
    w->win = (uint16_t)((w->win << 1) | (bit & 1U));
    if (w->cnt < POLARITY_WIN_SIZE) {
        w->cnt++;
    }
}

static uint8_t Polarity_WinAllZero(const PolarityWin_t *w)
{
    return (Polarity_WinFull(w) && (w->win == 0U));
}

static uint8_t Polarity_WinAllOne(const PolarityWin_t *w)
{
    return (Polarity_WinFull(w) && (w->win == (uint16_t)((1U << POLARITY_WIN_SIZE) - 1U)));
}

static void Polarity_ResetWindow(void)
{
    s_dirWin.win = 0U;
    s_dirWin.cnt = 0U;
}

static void Polarity_ForceUnpowered(void)
{
    Polarity_ResetWindow();
    if (s_state != POLARITY_UNPOWERED) {
        s_state = POLARITY_UNPOWERED;
        s_u8PendingState = (uint8_t)POLARITY_UNPOWERED;
    }
}

/* 极性命令是带数据通道；队列满只告警计数，不阻塞扫描线程 */
static void Polarity_SendArb(uint8_t device_id, uint8_t cmd_type, uint8_t duty_pct)
{
    rt_err_t ret = Arb_SendCommand(POLARITY_ARB_AXIS_ID,
                                   device_id,
                                   (uint8_t)PRIO_POWER,
                                   cmd_type,
                                   duty_pct,
                                   RT_FALSE);

    if (ret != RT_EOK) {
        s_arb_send_fail_count++;
        ARB_PRINT("send fail axis=%u dev=%u cmd=%u err=%d count=%u",
                  (unsigned)POLARITY_ARB_AXIS_ID,
                  (unsigned)device_id,
                  (unsigned)cmd_type,
                  (int)ret,
                  (unsigned)s_arb_send_fail_count);
    }
}

/* 稳定判定：窗口未满或不稳定 -> UNKNOWN（保持上次稳定态）。
   单管两态：全 0 = FWD（导通低有效），全 1 = REV（截止上拉高） */
static PolarityState_t Polarity_Eval(void)
{
    if (Polarity_WinAllZero(&s_dirWin)) return POLARITY_FWD;   /* PB15=0：正向（管子导通） */
    if (Polarity_WinAllOne(&s_dirWin))  return POLARITY_REV;   /* PB15=1：反向（截止上拉高） */
    return POLARITY_UNKNOWN;                                   /* 未满窗/抖动 */
}

/* ============ 接口 ============ */
void Polarity_Init(void)
{
    s_state = POLARITY_UNKNOWN;
    s_dirWin.win = 0U; s_dirWin.cnt = 0U;
    s_u8PendingState = 0U;
    s_bInit = 1U;
    POLARITY_PRINT("init win=%u", (unsigned)POLARITY_WIN_SIZE);
}

void Polarity_Scan(void)
{
    PolarityState_t st;

    if (!s_bInit) {
        return;
    }

#if POLARITY_SIM_MODE_EN
    st = g_pol_sim_state;           /* 模拟：直接取表达式窗口的状态 */
    if ((st == POLARITY_UNKNOWN) || ((uint8_t)st > (uint8_t)POLARITY_ABNORMAL)) {
        return;                     /* UNKNOWN/非法值：保持上次状态，不发事件 */
    }
#else
    if (BusVoltage_IsUnderVoltage() != 0U) {
        /* 单管方向检测依赖 24V：欠压期间 PB15 上拉高不代表 REV，欠压即判掉电；
           电压恢复（过欠压阈值+迟滞）后清窗重判，必须重新等满 5 点稳定窗口。
           门控只依赖欠压阈值（Flash 配置）；若配置被改坏，仲裁 RemoveOpposite
           在重上电极性沿上清除反方向残留兜底。 */
        Polarity_ForceUnpowered();
        return;
    }

    uint8_t d;
    d = rt_pin_read(POWER_DIR_PIN) ? 1U : 0U;

    Polarity_WinPush(&s_dirWin, d);

    st = Polarity_Eval();
    if (st == POLARITY_UNKNOWN) {
        return;                     /* 未满窗/不稳定：保持上次状态，不发事件 */
    }
#endif
    if (st != s_state) {            /* 稳定状态跳变沿才发事件（不重复发相同事件） */
        s_state = st;
        s_u8PendingState = (uint8_t)st;   /* 由线程上下文 Polarity_PrintPending 打印 */
        switch (st) {
        case POLARITY_FWD:
            Act_Event_Send(EVT_ACT_POLARITY_FWD);
            Sys_Event_Send(EVT_SYS_POLARITY_CHG);   /* 方向沿通知状态机（故障清除决策在 dev_state） */
            Polarity_SendArb((uint8_t)DEV_ID_POWER_POS,
                             (uint8_t)CMD_TYPE_RUN_FWD,
                             POLARITY_ARB_RUN_DUTY_PCT);
            break;
        case POLARITY_REV:
            Act_Event_Send(EVT_ACT_POLARITY_REV);
            Sys_Event_Send(EVT_SYS_POLARITY_CHG);   /* 方向沿通知状态机（故障清除决策在 dev_state） */
            Polarity_SendArb((uint8_t)DEV_ID_POWER_NEG,
                             (uint8_t)CMD_TYPE_RUN_REV,
                             POLARITY_ARB_RUN_DUTY_PCT);
            break;
        /* UNPOWERED/ABNORMAL：单管方案不可达（枚举保留，回退双管方案时恢复分支） */
        default: break;
        }
    }
}

/* 线程上下文：打印未处理的极性跳变（ISR 只置位，这里才打，避免 ISR 内打印） */
void Polarity_PrintPending(void)
{
    rt_base_t level;
    uint8_t st;
    uint32_t t_us = 0U;

    level = rt_hw_interrupt_disable();   /* 原子读清（ISR 可能正在置位） */
    st = s_u8PendingState;
    s_u8PendingState = 0U;
    rt_hw_interrupt_enable(level);

    if (st == 0U) {
        return;
    }
    UsTimer_UpdateTimestamp();
    t_us = (uint32_t)UsTimer_GetTimestampUs();
    switch ((PolarityState_t)st) {
    case POLARITY_UNPOWERED: POLARITY_PRINT("power lost t=%uus", t_us); break;
    case POLARITY_FWD:       POLARITY_PRINT("fwd t=%uus", t_us);            break;
    case POLARITY_REV:       POLARITY_PRINT("rev t=%uus", t_us);            break;
    case POLARITY_ABNORMAL:  POLARITY_PRINT("abnormal t=%uus", t_us);       break;
    default: break;
    }
}

PolarityState_t Polarity_GetState(void)
{
    return s_state;
}

/* EOF */
