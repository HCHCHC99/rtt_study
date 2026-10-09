/**
 * @file    hc32_drv_pwm.c
 * @brief   电机 PWM 硬件适配实现：TMR4_3（现行）/ TMRA4（旧实现保留）
 * @note    TMR4 语义（对齐参考工程 dev_motor 的运行/停止极性行为）：
 *          - H/L 均为低有效 OCMR：count=0(无匹配)→低、上计数匹配→高、peak→保持，
 *            低电平占比 = duty%（比较值 = (period+1)*duty/100）；
 *            L 通道事件依赖 H 匹配状态联合判定（OCCR_L 恒等于 OCCR_H）；
 *          - 运行极性 POCR=HOLD/HOLD（组内 H/L 同相，对齐参考"运行全低有效"）；
 *            停止极性 POCR=HOLD/INVT（L 反相 → 组内互补 50% = 混合极性，端压差≈0）；
 *          - OCCR/OCMR peak 缓冲装载：运行中改比较值无毛刺；
 *          - OC 关断 = 恒低（安全态），占空比 0 不经过比较值（规避 cmp=0 事件歧义）。
 */
#include "hc32_drv_pwm.h"
#include "hc32_ll.h"
#include "hc32_ll_pwc.h"
#include "hc32_ll_fcg.h"
#include "hc32_ll_clk.h"
#include <string.h>

#if PWM_DRV_USE_TMR4

/* ============ 本地状态 ============ */
static uint16_t s_tmr4_period = 0U;

/* 组 → OC 单通道映射 {H, L}（OC 接口按单通道寻址：UH=0 UL=1 VH=2 VL=3） */
static const uint8_t s_grp_oc[2][2] = {
    { PWM_OC_UH, PWM_OC_UL },   /* 组 U（引脚绑定见 hc32_drv_pwm.h） */
    { PWM_OC_VH, PWM_OC_VL },   /* 组 V（引脚绑定见 hc32_drv_pwm.h） */
};

uint16_t PwmHw4_GetPeriod(void)
{
    return s_tmr4_period;
}

uint16_t PwmHw4_DutyToCompare(uint32_t duty_pct)
{
    if (duty_pct > 100U) {
        duty_pct = 100U;
    }
    return (uint16_t)(((uint32_t)s_tmr4_period + 1U) * duty_pct / 100U);
}

/* 4 引脚复用到 TIM4_3（功能码 PWM_GPIO_FUNC_TMR4；引脚绑定见 hc32_drv_pwm.h） */
static void PwmHw4_GpioInit(void)
{
    LL_PERIPH_WE(LL_PERIPH_GPIO);
    GPIO_SetFunc(PWM_OUH_PORT, PWM_OUH_PIN, PWM_GPIO_FUNC_TMR4);
    GPIO_SetFunc(PWM_OUL_PORT, PWM_OUL_PIN, PWM_GPIO_FUNC_TMR4);
    GPIO_SetFunc(PWM_OVH_PORT, PWM_OVH_PIN, PWM_GPIO_FUNC_TMR4);
    GPIO_SetFunc(PWM_OVL_PORT, PWM_OVL_PIN, PWM_GPIO_FUNC_TMR4);
    LL_PERIPH_WP(LL_PERIPH_GPIO);
}

/* H 通道 OCMRH：低有效（count=0 无匹配→低；上计数匹配→高；peak→保持） */
static void PwmHw4_OcmrH(uint8_t ch_h)
{
    un_tmr4_oc_ocmrh_t ocmr;

    ocmr.OCMRx = 0U;                        /* 全部事件默认 HOLD */
    ocmr.OCMRx_f.OPNZRH = TMR4_OC_LOW;      /* count=0（无匹配）→ 低 */
    ocmr.OCMRx_f.OPUCH  = TMR4_OC_HIGH;     /* 上计数到达比较值 → 高 */
    TMR4_OC_SetHighChCompareMode(PWM_TMR4_UNIT, ch_h, ocmr);
}

/* L 通道 OCMRL：与 H 同相低有效（L 事件依赖 H 匹配状态联合判定，OCCR_L 必须 == OCCR_H）
   count=0（H/L 均不匹配）→ 低；上计数 H 匹配且 L 匹配 → 高；peak 无匹配 → 保持 */
static void PwmHw4_OcmrL(uint8_t ch_l)
{
    un_tmr4_oc_ocmrl_t ocmr;

    ocmr.OCMRx = 0U;                        /* 全部事件默认 HOLD */
    ocmr.OCMRx_f.OPNZRL = TMR4_OC_LOW;      /* count=0（H/L 均不匹配）→ 低 */
    ocmr.OCMRx_f.EOPUCL = TMR4_OC_HIGH;     /* 上计数 H 匹配 & L 匹配 → 高 */
    TMR4_OC_SetLowChCompareMode(PWM_TMR4_UNIT, ch_l, ocmr);
}

void PwmHw4_Init(uint32_t freq_hz)
{
    stc_tmr4_oc_init_t oc_init;
    stc_tmr4_pwm_init_t pwm_init;
    stc_clock_freq_t stcClkFreq;
    uint32_t u32Pclk1;
    uint8_t grp;

    if (freq_hz == 0U) {
        freq_hz = PWM_FREQ_DFT_HZ;
    }

    /* FCG 时钟门控（写保护域） */
    LL_PERIPH_WE(LL_PERIPH_FCG);
    FCG_Fcg2PeriphClockCmd(PWM_TMR4_CLK, ENABLE);
    LL_PERIPH_WP(LL_PERIPH_FCG);

    PwmHw4_GpioInit();

    /* 周期值：TMR4 计数时钟 = PCLK1（与 TMR6 同源，参考 hc32_drv_timer） */
    if (LL_OK == CLK_GetClockFreq(&stcClkFreq)) {
        u32Pclk1 = stcClkFreq.u32Pclk1Freq;
    } else {
        u32Pclk1 = CLK_GetBusClockFreq(CLK_BUS_PCLK1);
    }
    s_tmr4_period = (uint16_t)(u32Pclk1 / freq_hz - 1U);

    /* 基础计数器：内部时钟、不分频、锯齿波向上计数 */
    TMR4_SetClockSrc(PWM_TMR4_UNIT, TMR4_CLK_SRC_INTERNCLK);
    TMR4_SetClockDiv(PWM_TMR4_UNIT, TMR4_CLK_DIV1);
    TMR4_SetCountMode(PWM_TMR4_UNIT, TMR4_MD_SAWTOOTH);
    TMR4_SetPeriodValue(PWM_TMR4_UNIT, s_tmr4_period);

    /* U/V 两组：H/L 单通道 OC（比较值 + peak 缓冲 + 低有效 OCMR）+ 停止极性 + 启动 */
    for (grp = PWM_GRP_U; grp <= PWM_GRP_V; grp++) {
        uint8_t ch_h = s_grp_oc[grp][0];
        uint8_t ch_l = s_grp_oc[grp][1];

        /* H/L 两通道分别 OC_Init（比较值同值：L 波形依赖 H/L 同点匹配） */
        (void)TMR4_OC_StructInit(&oc_init);
        oc_init.u16CompareValue        = PwmHw4_DutyToCompare(PWM_DUTY_STOP);
        oc_init.u16OcInvalidPolarity   = TMR4_OC_INVD_LOW;        /* OC 关断输出低 = 安全态 */
        oc_init.u16CompareModeBufCond  = TMR4_OC_BUF_COND_PEAK;
        oc_init.u16CompareValueBufCond = TMR4_OC_BUF_COND_PEAK;   /* 改比较值 peak 装载，无毛刺 */
        oc_init.u16BufLinkTransObject  = 0U;
        (void)TMR4_OC_Init(PWM_TMR4_UNIT, ch_h, &oc_init);
        (void)TMR4_OC_Init(PWM_TMR4_UNIT, ch_l, &oc_init);

        PwmHw4_OcmrH(ch_h);
        PwmHw4_OcmrL(ch_l);

        /* POCR（耦合通道）：直通模式；上电安全态 = 停止态（L 反相 = 混合极性 50%） */
        (void)TMR4_PWM_StructInit(&pwm_init);                     /* THROUGH + CLK_DIV1 + HOLD/HOLD */
        pwm_init.u16Polarity = TMR4_PWM_OXH_HOLD_OXL_INVT;
        (void)TMR4_PWM_Init(PWM_TMR4_UNIT, grp, &pwm_init);

        TMR4_OC_Cmd(PWM_TMR4_UNIT, ch_h, ENABLE);
        TMR4_OC_Cmd(PWM_TMR4_UNIT, ch_l, ENABLE);
        TMR4_PWM_StartReloadTimer(PWM_TMR4_UNIT, grp);
    }

    TMR4_Start(PWM_TMR4_UNIT);
}

void PwmHw4_SetCompare(uint8_t grp, uint16_t cmp)
{
    /* 组内 H/L 同值同写：L 通道波形依赖 H/L 同一计数点匹配，两 OCCR 必须恒等 */
    TMR4_OC_SetCompareValue(PWM_TMR4_UNIT, s_grp_oc[grp][0], cmp);
    TMR4_OC_SetCompareValue(PWM_TMR4_UNIT, s_grp_oc[grp][1], cmp);
}

void PwmHw4_SetRunPolarity(uint8_t grp)
{
    TMR4_PWM_SetPolarity(PWM_TMR4_UNIT, grp, TMR4_PWM_OXH_HOLD_OXL_HOLD);
}

void PwmHw4_SetStopPolarity(uint8_t grp)
{
    TMR4_PWM_SetPolarity(PWM_TMR4_UNIT, grp, TMR4_PWM_OXH_HOLD_OXL_INVT);
}

void PwmHw4_OutputCmd(uint8_t grp, en_functional_state_t enNewState)
{
    /* 组内 H/L 一起使能/关断（关断 = 恒低安全态） */
    TMR4_OC_Cmd(PWM_TMR4_UNIT, s_grp_oc[grp][0], enNewState);
    TMR4_OC_Cmd(PWM_TMR4_UNIT, s_grp_oc[grp][1], enNewState);
}

#else /* ===================== 旧 TMRA4 实现（保留，PWM_DRV_USE_TMR4=0） ===================== */

#include <rtthread.h>

/* board.c 的 SysTick 忙等延时；rtthread.h 未声明，显式补充 */
extern void rt_hw_us_delay(rt_uint32_t us);

/* 每个 TMRA 单元记录当前周期值（set duty 换算用；同单元重复初始化幂等） */
typedef struct {
    CM_TMRA_TypeDef *tmra;
    uint32_t period;
} PwmTimerPeriod_t;

static PwmTimerPeriod_t s_timer_period[2];
static uint8_t s_timer_num = 0U;

/* 诊断：100us 窗口两次采样计数器，反推 TMRA 实际计数时钟（Hz） */
uint32_t PwmHw_ProbeCountClock(CM_TMRA_TypeDef *tmra, uint32_t period)
{
    uint32_t c1 = TMRA_GetCountValue(tmra);
    rt_hw_us_delay(100U);
    uint32_t c2 = TMRA_GetCountValue(tmra);
    uint32_t delta = (c1 >= c2) ? (c1 - c2) : ((period + 1U) + c1 - c2);
    return delta * 10000U;   /* 100us 窗口 → Hz */
}

static uint32_t *PwmHw_PeriodOf(CM_TMRA_TypeDef *tmra)
{
    uint8_t i;

    for (i = 0U; i < s_timer_num; i++) {
        if (s_timer_period[i].tmra == tmra) {
            return &s_timer_period[i].period;
        }
    }
    if (s_timer_num < (uint8_t)(sizeof(s_timer_period) / sizeof(s_timer_period[0]))) {
        s_timer_period[s_timer_num].tmra = tmra;
        s_timer_period[s_timer_num].period = PWM_PERIOD_DFT;
        return &s_timer_period[s_timer_num++].period;
    }
    return RT_NULL;
}

void PwmHw_ChannelInit(uint8_t u8Port, uint16_t u16Pin, CM_TMRA_TypeDef *TMRAx,
                       uint32_t u32Ch, float fInitDutyPct, uint8_t u8LowActive)
{
    stc_tmra_init_t tmra_init;
    stc_tmra_pwm_init_t pwm_init;
    uint32_t *period = PwmHw_PeriodOf(TMRAx);
    uint16_t cmp_val;

    (void)memset(&tmra_init, 0, sizeof(tmra_init));
    (void)memset(&pwm_init, 0, sizeof(pwm_init));

    tmra_init.u8CountSrc = TMRA_CNT_SRC_SW;     /* 软件计数（此前未初始化：栈垃圾曾选中硬件计数源，配置未生效） */
    if (period == RT_NULL) {
        return;
    }

    /* 外设时钟门控（FCG 写保护域：必须先解锁，参考 hc_drv_timer） */
    LL_PERIPH_WE(LL_PERIPH_FCG);
    if (TMRAx == CM_TMRA_1) {
        FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMRA_1, ENABLE);
    } else if (TMRAx == CM_TMRA_2) {
        FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMRA_2, ENABLE);
    } else if (TMRAx == CM_TMRA_3) {
        FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMRA_3, ENABLE);
    } else if (TMRAx == CM_TMRA_4) {
        FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMRA_4, ENABLE);
    } else {
        return;
    }
    LL_PERIPH_WP(LL_PERIPH_FCG);

    /* 引脚复用到 TMRA（功能码 4，与 hkb_1 的 Func_Tima0 等值） */
    LL_PERIPH_WE(LL_PERIPH_GPIO);
    GPIO_SetFunc(u8Port, u16Pin, PWM_GPIO_FUNC_TIMA);
    LL_PERIPH_WP(LL_PERIPH_GPIO);

    /* TMRA 基础：PCLK 直驱、锯齿波、向上计数 */
    tmra_init.sw_count.u16ClockDiv = TMRA_CLK_DIV1;
    tmra_init.sw_count.u16CountMode = TMRA_MD_SAWTOOTH;
    tmra_init.sw_count.u16CountDir = TMRA_DIR_UP;
    tmra_init.hw_count.u16CountUpCond = TMRA_CNT_UP_COND_INVD;
    tmra_init.hw_count.u16CountDownCond = TMRA_CNT_UP_COND_INVD;
    tmra_init.u32PeriodValue = *period;
    (void)TMRA_Init(TMRAx, &tmra_init);

    /* PWM 通道：低有效（比较匹配=低，周期匹配=高，起停=低），占空比=低电平占比 */
    cmp_val = (uint16_t)(((uint32_t)(*period + 1U) * (uint32_t)fInitDutyPct) / 100U);
    pwm_init.u32CompareValue = cmp_val;
    if (u8LowActive != 0U) {
        pwm_init.u16StartPolarity = TMRA_PWM_LOW;
        pwm_init.u16StopPolarity = TMRA_PWM_LOW;
        pwm_init.u16CompareMatchPolarity = TMRA_PWM_LOW;
        pwm_init.u16PeriodMatchPolarity = TMRA_PWM_HIGH;
    } else {
        pwm_init.u16StartPolarity = TMRA_PWM_HIGH;
        pwm_init.u16StopPolarity = TMRA_PWM_HIGH;
        pwm_init.u16CompareMatchPolarity = TMRA_PWM_HIGH;
        pwm_init.u16PeriodMatchPolarity = TMRA_PWM_LOW;
    }
    (void)TMRA_PWM_Init(TMRAx, u32Ch, &pwm_init);
    TMRA_PWM_OutputCmd(TMRAx, u32Ch, ENABLE);

    TMRA_Start(TMRAx);
}

void PwmHw_SetDutyPct(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch, float fDutyPct)
{
    uint32_t *period = PwmHw_PeriodOf(TMRAx);
    uint32_t cmp_val;

    if ((period == RT_NULL) || (fDutyPct < 0.0f) || (fDutyPct > 100.0f)) {
        return;
    }

    if (fDutyPct <= 0.0f) {
        cmp_val = 0U;                                   /* 恒低 */
    } else if (fDutyPct >= 100.0f) {
        cmp_val = *period;                              /* 恒高 */
    } else {
        cmp_val = (uint32_t)(((uint32_t)*period * (uint32_t)fDutyPct) / 100U);
    }
    TMRA_SetCompareValue(TMRAx, u32Ch, cmp_val);
}

void PwmHw_SetCompareValue(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch, uint32_t u32CmpVal)
{
    TMRA_SetCompareValue(TMRAx, u32Ch, u32CmpVal);
}

void PwmHw_SetForcePolarity(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch, uint16_t u16Polarity)
{
    TMRA_PWM_SetForcePolarity(TMRAx, u32Ch, u16Polarity);
}

void PwmHw_CompareEnable(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch)
{
    TMRA_PWM_OutputCmd(TMRAx, u32Ch, ENABLE);
}

void PwmHw_CompareDisable(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch)
{
    TMRA_PWM_OutputCmd(TMRAx, u32Ch, DISABLE);
}

#endif /* PWM_DRV_USE_TMR4 */

/* EOF */
