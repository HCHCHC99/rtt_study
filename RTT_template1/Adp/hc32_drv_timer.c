#include "hc32_drv_timer.h"
/**
 * @file    hc_drv_timer.c
 * @brief   HC32 TMR6 微秒级时间基准驱动（实现 Utils/us_timer 接口）
 * @note    移植自 HB_chuchai_v6.0.4；TMR6_2，PCLK0/64，16 位锯齿（回绕）计数
 */
#include "us_timer.h"
#include "rtt_manager.h"
#include "hc32_ll_tmr6.h"
#include "hc32_ll_tmr0.h"
#include <rtthread.h>


/* 全局变量 */
static uint32_t m_u32TimerFreq = 0;        /* Timer6 实际计数频率 Hz */
static volatile uint32_t m_u32LastCounter = 0;
static volatile uint32_t m_u32DeltaCounter = 0;
static volatile uint64_t s_total_elapsed_us = 0;
static uint32_t s_last_timer6_counter = 0;
static uint8_t  s_timestamp_initialized = 0;

void Timer6_Timebase_Init(void)
{
    stc_timer6_init_t stcTmrInit;
    stc_clock_freq_t stcClkFreq;
    int32_t i32Ret;

    /* 时钟频率：用 CLK_GetClockFreq 取真实 PCLK0（源工程做法，避免 SystemCoreClock 未刷新） */
    if (LL_OK == CLK_GetClockFreq(&stcClkFreq)) {
        SystemCoreClock = stcClkFreq.u32SysclkFreq;
        m_u32TimerFreq  = stcClkFreq.u32Pclk0Freq / TMR6_DIV_VAL;
    } else {
        SystemCoreClockUpdate();
        m_u32TimerFreq = CLK_GetBusClockFreq(CLK_BUS_PCLK0) / TMR6_DIV_VAL;
    }

    /* 使能 TMR6_2 时钟 */
    FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMR6_2, ENABLE);

    /* 复位 TMR6_2 */
    TMR6_DeInit(CM_TMR6_2);

    /* 初始化结构体为默认值 */
    TMR6_StructInit(&stcTmrInit);

    /* 自由计数：软件时钟源、向上计数、PCLK0/64 分频、锯齿（回绕） */
    stcTmrInit.u8CountSrc = TMR6_CNT_SRC_SW;
    stcTmrInit.sw_count.u32CountDir  = TMR6_CNT_UP;
    stcTmrInit.sw_count.u32ClockDiv  = TMR6_CLK_DIV64;
    stcTmrInit.sw_count.u32CountMode = TMR6_MD_SAWTOOTH;
    stcTmrInit.u32PeriodValue = TMR6_PERIOD_MAX;

    (void)TMR6_Init(CM_TMR6_2, &stcTmrInit);

    /* 清标志、关中断（纯时基，不进中断） */
    TMR6_ClearStatus(CM_TMR6_2, TMR6_FLAG_CLR_ALL);
    TMR6_IntCmd(CM_TMR6_2, TMR6_INT_ALL, DISABLE);

    m_u32LastCounter = 0;
    m_u32DeltaCounter = 0;
    s_timestamp_initialized = 0;
}

void Timer6_Timebase_Start(void) { TMR6_Start(CM_TMR6_2); }
void Timer6_Timebase_Stop(void)  { TMR6_Stop(CM_TMR6_2); }
uint32_t Timer6_Timebase_GetCounter(void)   { return TMR6_GetCountValue(CM_TMR6_2); }
uint32_t Timer6_Timebase_GetFrequency(void) { return m_u32TimerFreq; }

uint32_t Timer6_Timebase_GetDelta(void)
{
    uint32_t u32CurrentCounter = Timer6_Timebase_GetCounter();
    uint32_t u32Delta = 0;

    if (u32CurrentCounter >= m_u32LastCounter) {
        u32Delta = u32CurrentCounter - m_u32LastCounter;
    } else {
        u32Delta = (65536u - m_u32LastCounter) + u32CurrentCounter;
    }

    m_u32LastCounter = u32CurrentCounter;
    m_u32DeltaCounter = u32Delta;
    return u32Delta;
}

uint32_t Timer6_Timebase_DeltaToUs(uint32_t u32DeltaCounter)
{
    if (m_u32TimerFreq == 0) return 0;
    return (u32DeltaCounter * 1000000ul) / m_u32TimerFreq;
}

uint32_t Timer6_Timebase_DeltaToMs(uint32_t u32DeltaCounter)
{
    if (m_u32TimerFreq == 0) return 0;
    return (u32DeltaCounter * 1000ul) / m_u32TimerFreq;
}

uint32_t Timer6_Timebase_GetPulseInterval(void)
{
    uint32_t u32Delta = Timer6_Timebase_GetDelta();
    return Timer6_Timebase_DeltaToUs(u32Delta);
}

uint32_t Timer6_Timebase_GetPulseTime(uint8_t pulse_count)
{
    if (pulse_count < 2) return 0;
    uint32_t delta = Timer6_Timebase_GetDelta();
    uint32_t time_us = Timer6_Timebase_DeltaToUs(delta);
    return time_us * (pulse_count - 1);
}

float Timer6_Timebase_CalculateRPM(uint32_t time_us, uint8_t pulses, uint8_t pulses_per_rev)
{
    if (time_us == 0 || pulses == 0 || pulses_per_rev == 0) return 0.0f;
    float rpm = (float)pulses * 60000000.0f;
    rpm /= (float)pulses_per_rev * (float)time_us;
    return rpm;
}

void Timer6_Timebase_UpdateTimestamp(void)
{
    uint32_t current_counter = Timer6_Timebase_GetCounter();
    uint32_t delta;

    if (!s_timestamp_initialized) {
        s_last_timer6_counter = current_counter;
        s_timestamp_initialized = 1;
        return;
    }

    if (current_counter >= s_last_timer6_counter) {
        delta = current_counter - s_last_timer6_counter;
    } else {
        delta = (0x10000u - s_last_timer6_counter) + current_counter;
    }

    uint32_t delta_us = Timer6_Timebase_DeltaToUs(delta);
    s_total_elapsed_us += delta_us;
    s_last_timer6_counter = current_counter;
}

uint64_t Timer6_Timebase_GetTimestamp(void)
{
    return s_total_elapsed_us;
}

void Print_All_Clock_Freq(void)
{
    stc_clock_freq_t stcClkFreq;

    if (LL_OK == CLK_GetClockFreq(&stcClkFreq)) {
        MAIN_D("[CLK] ==========================================");
        MAIN_D("[CLK] SYSCLK=%u\n", (unsigned)stcClkFreq.u32SysclkFreq);
        MAIN_D("[CLK] HCLK=%u\n", (unsigned)stcClkFreq.u32HclkFreq);
        MAIN_D("[CLK] PCLK0=%u\n", (unsigned)stcClkFreq.u32Pclk0Freq);
        MAIN_D("[CLK] PCLK1=%u\n", (unsigned)stcClkFreq.u32Pclk1Freq);
        MAIN_D("[CLK] PCLK2=%u\n", (unsigned)stcClkFreq.u32Pclk2Freq);
        MAIN_D("[CLK] PCLK3=%u\n", (unsigned)stcClkFreq.u32Pclk3Freq);
        MAIN_D("[CLK] PCLK4=%u\n", (unsigned)stcClkFreq.u32Pclk4Freq);
        MAIN_D("[CLK] EXCLK=%u\n", (unsigned)stcClkFreq.u32ExclkFreq);
        MAIN_D("[CLK] ==========================================");
    } else {
        MAIN_D("[CLK] GetClockFreq failed\n");
    }
}


/* ==================== us_timer 通用接口表实现 ==================== */
static int hc_us_timer_init(void)             { Timer6_Timebase_Init(); return 0; }
static void hc_us_timer_start(void)           { Timer6_Timebase_Start(); }
static void hc_us_timer_stop(void)            { Timer6_Timebase_Stop(); }
static void hc_us_timer_update_ts(void)       { Timer6_Timebase_UpdateTimestamp(); }
static uint64_t hc_us_timer_get_ts(void)      { return Timer6_Timebase_GetTimestamp(); }
static uint32_t hc_us_timer_get_counter(void) { return Timer6_Timebase_GetCounter(); }
static uint32_t hc_us_timer_delta_to_us(uint32_t cnt) { return Timer6_Timebase_DeltaToUs(cnt); }
static uint32_t hc_us_timer_get_delta(void)   { return Timer6_Timebase_GetDelta(); }

const struct us_timer_ops hc_us_timer_ops = {
    hc_us_timer_init,
    hc_us_timer_start,
    hc_us_timer_stop,
    hc_us_timer_update_ts,
    hc_us_timer_get_ts,
    hc_us_timer_get_counter,
    hc_us_timer_delta_to_us,
    hc_us_timer_get_delta,
};

/* ==================== 1ms 检测心跳（TMR0_2 CH_A） ==================== */

static void (*s_pfn1msCb)(void) = NULL;

static void HcDrv_Timer_1msIsr(void)
{
    rt_interrupt_enter();
    if (TMR0_GetStatus(TMR0_1MS_UNIT, TMR0_FLAG_CMP_A) == SET) {
        TMR0_ClearStatus(TMR0_1MS_UNIT, TMR0_FLAG_CMP_A);
        if (s_pfn1msCb != NULL) {
            s_pfn1msCb();
        }
    }
    rt_interrupt_leave();
}

int HcDrv_Timer_Start1ms(void (*pfnCb)(void))
{
    stc_tmr0_init_t stcTmr0;
    stc_irq_signin_config_t stcIrq;
    stc_clock_freq_t stcClkFreq;
    uint32_t u32Pclk1;
    uint16_t u16Compare;

    s_pfn1msCb = pfnCb;

    /* TMR0_2 时钟 */
    LL_PERIPH_WE(LL_PERIPH_FCG);
    FCG_Fcg2PeriphClockCmd(TMR0_1MS_CLK, ENABLE);
    LL_PERIPH_WP(LL_PERIPH_FCG);

    if (LL_OK == CLK_GetClockFreq(&stcClkFreq)) {
        u32Pclk1 = stcClkFreq.u32Pclk1Freq;
    } else {
        u32Pclk1 = CLK_GetBusClockFreq(CLK_BUS_PCLK1);
    }
    /* 1ms 比较值 = PCLK1/64 / 1000 - 1 */
    u16Compare = (uint16_t)((u32Pclk1 / TMR0_1MS_DIV_VAL) / 1000UL - 1UL);

    (void)TMR0_StructInit(&stcTmr0);
    stcTmr0.u32ClockDiv     = TMR0_1MS_CLK_DIV;
    stcTmr0.u32Func         = TMR0_FUNC_CMP;
    stcTmr0.u16CompareValue = u16Compare;
    stcTmr0.u32ClockSrc     = TMR0_CLK_SRC_INTERN_CLK;
    if (LL_OK != TMR0_Init(TMR0_1MS_UNIT, TMR0_1MS_CH, &stcTmr0)) {
        return -1;
    }
    TMR0_ClearStatus(TMR0_1MS_UNIT, TMR0_FLAG_ALL);
    TMR0_IntCmd(TMR0_1MS_UNIT, TMR0_INT_CMP_A, ENABLE);

    /* INTC 回调注册（与 ADC EOCA 同机制，IRQ007 向量已由 hc32_ll_interrupts.c 分发） */
    stcIrq.enIntSrc    = TMR0_1MS_IRQ_SRC;
    stcIrq.enIRQn      = TMR0_1MS_IRQn;
    stcIrq.pfnCallback = &HcDrv_Timer_1msIsr;
    LL_PERIPH_WE(LL_PERIPH_INTC);
    if (LL_OK != INTC_IrqSignIn(&stcIrq)) {
        LL_PERIPH_WP(LL_PERIPH_INTC);
        return -2;
    }
    LL_PERIPH_WP(LL_PERIPH_INTC);

    NVIC_ClearPendingIRQ(TMR0_1MS_IRQn);
    NVIC_SetPriority(TMR0_1MS_IRQn, TMR0_1MS_INT_PRIO);
    NVIC_EnableIRQ(TMR0_1MS_IRQn);

    TMR0_Start(TMR0_1MS_UNIT, TMR0_1MS_CH);
    MAIN_D("[TMR] 1ms heartbeat started (TMR0_2 cmp=%u)", (unsigned)u16Compare);
    return 0;
}

/* EOF */



