/**
 * @file    dev_hall_rod.c
 * @brief   推杆霍尔设备实现（GPIO 双窗口消抖 + 双低故障沿）
 * @note    仿 dev_polarity 窗口机制；不做打印/阻塞（Scan 由 rod_task 线程调用）。
 */
#include "dev_hall_rod.h"
#include "dev_state.h"        /* Sys_Event_Send */
#include "dev_event_def.h"
#include "rtt_manager.h"
#include "drv_gpio.h"                     /* GET_PIN / rt_pin_read */
#include <rtthread.h>

/* 双窗口：位掩码（bit0 最新）+ 已填点数 */
typedef struct {
    uint16_t win;
    uint8_t  cnt;
} RodHallWin_t;

static RodHallWin_t s_maxWin;
static RodHallWin_t s_minWin;
static RodHallWin_t s_ftWin;        /* 双低窗口：输入 = !(max_raw | min_raw) */
static bool s_atMax;
static bool s_atMin;
static bool s_fault;
static uint8_t s_bInit = 0U;

/* Watch 观测：bit0=上限位 bit1=下限位 bit2=双低故障 */
volatile uint8_t g_rodhall_dbg = 0U;

static void RodHall_WinPush(RodHallWin_t *w, uint8_t bit)
{
    w->win = (uint16_t)((w->win << 1) | (bit & 1U));
    if (w->cnt < ROD_HALL_WIN_SIZE) {
        w->cnt++;
    }
}

static uint8_t RodHall_WinAllOne(const RodHallWin_t *w)
{
    return ((w->cnt >= ROD_HALL_WIN_SIZE) &&
            (w->win == (uint16_t)((1U << ROD_HALL_WIN_SIZE) - 1U)));
}

static uint8_t RodHall_WinAllZero(const RodHallWin_t *w)
{
    return ((w->cnt >= ROD_HALL_WIN_SIZE) && (w->win == 0U));
}

void RodHall_Init(void)
{
    s_maxWin.win = 0U; s_maxWin.cnt = 0U;
    s_minWin.win = 0U; s_minWin.cnt = 0U;
    s_ftWin.win  = 0U; s_ftWin.cnt  = 0U;
    s_atMax = false;
    s_atMin = false;
    s_fault = false;
    g_rodhall_dbg = 0U;
    s_bInit = 1U;
    HALL_ROD_PRINT("init win=%u", (unsigned)ROD_HALL_WIN_SIZE);
}

void RodHall_Scan(void)
{
    uint8_t mx;
    uint8_t mn;
    bool fault_now;

    if (s_bInit == 0U) {
        return;
    }

    mx = (rt_pin_read(ROD_HALL_MAX_PIN) != 0) ? 1U : 0U;
    mn = (rt_pin_read(ROD_HALL_MIN_PIN) != 0) ? 1U : 0U;

    RodHall_WinPush(&s_maxWin, mx);
    RodHall_WinPush(&s_minWin, mn);
    // 修改点：双低故障 -> 两个都低时窗口推1
    RodHall_WinPush(&s_ftWin, (uint8_t)(!(mx | mn)));

    /* 窗口全 1 -> 稳定触发；全 0 -> 稳定释放 */
    if (RodHall_WinAllOne(&s_maxWin) != 0U) {
        s_atMax = true;
    } else if (RodHall_WinAllZero(&s_maxWin) != 0U) {
        s_atMax = false;
    }
    if (RodHall_WinAllOne(&s_minWin) != 0U) {
        s_atMin = true;
    } else if (RodHall_WinAllZero(&s_minWin) != 0U) {
        s_atMin = false;
    }

    /* 双低故障：消抖后上升沿发系统事件（EMERGENCY），只发跳变沿 */
    fault_now = (RodHall_WinAllOne(&s_ftWin) != 0U);
    if (fault_now && !s_fault) {
        Sys_Event_Send(EVT_SYS_ROD_LIMIT_FAULT);
        HALL_ROD_PRINT("rod hall fault (both low)");
    }
    s_fault = fault_now;

    g_rodhall_dbg = (uint8_t)((s_atMax ? 1U : 0U) |
                              (s_atMin ? 2U : 0U) |
                              (s_fault ? 4U : 0U));
}

bool RodHall_IsAtMax(void)  { return s_atMax; }
bool RodHall_IsAtMin(void)  { return s_atMin; }
bool RodHall_IsFault(void)  { return s_fault; }

/* EOF */
