/**
 * @file    adc_drv.c
 * @brief   ADC 硬件驱动实现（HC32F460）
 * @note    基于 HB_chuchai v6.0.4 Adc.c 裁剪：去 DMA；TMR0_1 CH_B 硬件触发 500us（AOS 事件路由）+ EOCA 滑动窗口
 */
#include "hc32_drv_adc.h"
#include "dev_adc_ops.h"   /* ADC_MEAN_WINDOW_MS：窗口时长（点数换算在 hc32_drv_adc.h） */
#include "hc32_ll_tmr0.h"
#include "hc32_ll_aos.h"
#include <rtthread.h>
#include <string.h>

/* ============ 本地状态 ============ */
static adc_drv_inst_t *s_pstcInsts = NULL;
static uint8_t s_u8InstNum = 0;
static bool s_bInitialized = false;

/* 滑动平均窗口静态存储（每实例一条，长度 ADC_MEAN_WINDOW_SAMPLES = 10ms @ 2kHz） */
static uint32_t s_au32MeanWin[HC32_ADC_MAX_INST][ADC_MEAN_WINDOW_SAMPLES];

/* ============ 本地函数 ============ */
static void AdcDrv_HwTriggerInit(void);
static void AdcDrv_SetPinAnalogMode(uint8_t u8Port, uint16_t u16Pin)
{
    stc_gpio_init_t stcGpioInit;
    (void)GPIO_StructInit(&stcGpioInit);
    stcGpioInit.u16PinAttr = PIN_ATTR_ANALOG;
    LL_PERIPH_WE(LL_PERIPH_GPIO);
    (void)GPIO_Init(u8Port, u16Pin, &stcGpioInit);
    LL_PERIPH_WP(LL_PERIPH_GPIO);
}

static void AdcDrv_IrqConfig(void)
{
    stc_irq_signin_config_t stcIrq;
    stcIrq.enIntSrc    = ADC_SEQA_INT_SRC;
    stcIrq.enIRQn      = ADC_SEQA_INT_IRQn;
    stcIrq.pfnCallback = &AdcDrv_ADC1_IRQHandler;
    LL_PERIPH_WE(LL_PERIPH_INTC);
    (void)INTC_IrqSignIn(&stcIrq);
    LL_PERIPH_WP(LL_PERIPH_INTC);
    NVIC_ClearPendingIRQ(stcIrq.enIRQn);
    NVIC_SetPriority(stcIrq.enIRQn, ADC_SEQA_INT_PRIO);
    NVIC_EnableIRQ(stcIrq.enIRQn);
    (void)ADC_IntCmd(ADC_UNIT, ADC_INT_EOCA, ENABLE);
}

/* ============ TMR0_1 + AOS 硬件触发（500us） ============ */
static void AdcDrv_HwTriggerInit(void)
{
    stc_tmr0_init_t stcTmr0;
    stc_clock_freq_t stcClkFreq;
    uint32_t u32Pclk1;
    uint32_t u32Freq;
    uint16_t u16Compare;

    /* AOS：TMR0_1 CMP_B 事件 -> ADC1 触发输入 0 */
    LL_PERIPH_WE(LL_PERIPH_FCG);
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_AOS, ENABLE);
    LL_PERIPH_WP(LL_PERIPH_FCG);
    AOS_SetTriggerEventSrc(ADC_TRIG_AOS_SEL, ADC_TRIG_AOS_EVT);

    /* TMR0_1 CH_B 时钟 */
    LL_PERIPH_WE(LL_PERIPH_FCG);
    FCG_Fcg2PeriphClockCmd(ADC_TRIG_TMR_CLK, ENABLE);
    LL_PERIPH_WP(LL_PERIPH_FCG);

    if (LL_OK == CLK_GetClockFreq(&stcClkFreq)) {
        u32Pclk1 = stcClkFreq.u32Pclk1Freq;
    } else {
        u32Pclk1 = CLK_GetBusClockFreq(CLK_BUS_PCLK1);
    }
    u32Freq = u32Pclk1 / ADC_TRIG_TMR_DIV_VAL;                        /* 分频后计数频率 */
    u16Compare = (uint16_t)((u32Freq / (1000000UL / ADC_SAMPLE_INTERVAL_US)) - 1UL);

    (void)TMR0_StructInit(&stcTmr0);
    stcTmr0.u32ClockDiv     = ADC_TRIG_TMR_CLK_DIV;
    stcTmr0.u32Func         = TMR0_FUNC_CMP;
    stcTmr0.u16CompareValue = u16Compare;
    stcTmr0.u32ClockSrc     = TMR0_CLK_SRC_INTERN_CLK;
    (void)TMR0_Init(ADC_TRIG_TMR_UNIT, ADC_TRIG_TMR_CH, &stcTmr0);
    TMR0_ClearStatus(ADC_TRIG_TMR_UNIT, TMR0_FLAG_ALL);

    /* ADC1 SEQ_A 选 EVT0 硬件触发并使能 */
    (void)ADC_TriggerConfig(ADC_UNIT, ADC_SEQ_A, ADC_TRIG_HARD_SEL);
    ADC_TriggerCmd(ADC_UNIT, ADC_SEQ_A, ENABLE);

    ADC_DRV_DBG("adc_drv hw trig: TMR0_1 %uus compare=%u", ADC_SAMPLE_INTERVAL_US, (unsigned)u16Compare);
}

/* ============ EOCA 中断处理 ============ */
void AdcDrv_ADC1_IRQHandler(void)
{
    (void)ADC_ClearStatus(ADC_UNIT, ADC_FLAG_EOCA);

    for (uint8_t i = 0U; i < s_u8InstNum; i++) {
        adc_drv_inst_t *pstc = &s_pstcInsts[i];
        uint16_t u16Raw = ADC_GetValue(ADC_UNIT, pstc->stcCh.u8Channel);
        pstc->u16LatestRaw = u16Raw;
        pstc->u32SampleCount++;

        float fVal = (float)u16Raw * ADC_VREF / (float)(1U << ADC_RES) * pstc->stcCh.fGain + pstc->stcCh.fOffset;
        if (pstc->stcCh.bAbs && fVal < 0.0f) {
            fVal = -fVal;
        }
        pstc->fLatest = fVal;
        pstc->afRing[pstc->u16WriteIdx] = fVal;
        pstc->u16WriteIdx = (uint16_t)((pstc->u16WriteIdx + 1U) % ADC_DRV_RING_SIZE);
        if (pstc->u16Count < ADC_DRV_RING_SIZE) {
            pstc->u16Count++;
        }

        /* 滑动平均窗口（原始值）：满窗后先减最旧再加最新 */
        if (pstc->pu32MeanWin != NULL) {
            if (pstc->u16MeanCnt < ADC_MEAN_WINDOW_SAMPLES) {
                pstc->u32MeanSum += u16Raw;
                pstc->pu32MeanWin[pstc->u16MeanIdx] = u16Raw;
                pstc->u16MeanIdx = (uint16_t)((pstc->u16MeanIdx + 1U) % ADC_MEAN_WINDOW_SAMPLES);
                pstc->u16MeanCnt++;
            } else {
                pstc->u32MeanSum -= pstc->pu32MeanWin[pstc->u16MeanIdx];
                pstc->u32MeanSum += u16Raw;
                pstc->pu32MeanWin[pstc->u16MeanIdx] = u16Raw;
                pstc->u16MeanIdx = (uint16_t)((pstc->u16MeanIdx + 1U) % ADC_MEAN_WINDOW_SAMPLES);
            }
        }

        if (pstc->stcCh.pfnCallback != NULL) {
            pstc->stcCh.pfnCallback(u16Raw, pstc->stcCh.u8Channel);
        }
    }
}

/* ============ 初始化 ============ */
int AdcDrv_Init(const adc_drv_ch_t *pstcChTable, uint8_t u8ChNum, adc_drv_inst_t *pstcInsts)
{
    if (pstcChTable == NULL || pstcInsts == NULL || u8ChNum == 0U) {
        return -1;
    }
    if (s_bInitialized) {
        return 0;   /* 幂等 */
    }

    s_pstcInsts = pstcInsts;
    s_u8InstNum = u8ChNum;
    (void)memset(s_pstcInsts, 0, sizeof(adc_drv_inst_t) * u8ChNum);
    for (uint8_t i = 0U; i < u8ChNum; i++) {
        s_pstcInsts[i].u8Id = i;
        s_pstcInsts[i].stcCh = pstcChTable[i];
        s_pstcInsts[i].pu32MeanWin = s_au32MeanWin[i];
    }

    /* ADC 时钟 */
    LL_PERIPH_WE(LL_PERIPH_FCG);
    FCG_Fcg3PeriphClockCmd(ADC_PERIPH_CLK, ENABLE);
    LL_PERIPH_WP(LL_PERIPH_FCG);

    /* ADC 初始化：SEQ_A 单次 */
    stc_adc_init_t stcAdcInit;
    (void)ADC_StructInit(&stcAdcInit);
    stcAdcInit.u16ScanMode = ADC_MD_SEQA_SINGLESHOT;
    (void)ADC_Init(ADC_UNIT, &stcAdcInit);

    /* 逐通道：引脚模拟模式 + 使能 SEQ_A */
    for (uint8_t i = 0U; i < u8ChNum; i++) {
        adc_drv_inst_t *pstc = &s_pstcInsts[i];
        AdcDrv_SetPinAnalogMode(pstc->stcCh.u8Port, pstc->stcCh.u16Pin);
        (void)ADC_ChCmd(ADC_UNIT, ADC_SEQ_A, pstc->stcCh.u8Channel, ENABLE);
    }

    /* EOCA 中断 */
    AdcDrv_IrqConfig();

    /* TMR0_1 + AOS 硬件触发（仅配置，不启动；AdcDrv_Start 再启动采样） */
    AdcDrv_HwTriggerInit();

    s_bInitialized = true;
    ADC_DRV_DBG("adc_drv init: %u channel(s)", u8ChNum);
    return 0;
}

void AdcDrv_SoftwareTrigger(void)
{
    if (!s_bInitialized) return;
    (void)ADC_Start(ADC_UNIT);
}

void AdcDrv_Start(void)
{
    if (!s_bInitialized) return;
    TMR0_Start(ADC_TRIG_TMR_UNIT, ADC_TRIG_TMR_CH);
    ADC_DRV_DBG("adc_drv start: TMR0_1 %uus hw trig", ADC_SAMPLE_INTERVAL_US);
}

void AdcDrv_Stop(void)
{
    TMR0_Stop(ADC_TRIG_TMR_UNIT, ADC_TRIG_TMR_CH);
}

uint16_t AdcDrv_GetLatestRaw(uint8_t u8Id)
{
    if (u8Id >= s_u8InstNum) return 0U;
    return s_pstcInsts[u8Id].u16LatestRaw;
}

float AdcDrv_GetLatest(uint8_t u8Id)
{
    if (u8Id >= s_u8InstNum) return 0.0f;
    return s_pstcInsts[u8Id].fLatest;
}

float AdcDrv_GetMean(uint8_t u8Id)
{
    if (u8Id >= s_u8InstNum) return 0.0f;
    adc_drv_inst_t *pstc = &s_pstcInsts[u8Id];
    if (pstc->u16MeanCnt == 0U) return 0.0f;
    uint16_t u16Avg = (uint16_t)(pstc->u32MeanSum / pstc->u16MeanCnt);
    float fVal = (float)u16Avg * ADC_VREF / (float)(1U << ADC_RES) * pstc->stcCh.fGain + pstc->stcCh.fOffset;
    if (pstc->stcCh.bAbs && fVal < 0.0f) {
        fVal = -fVal;
    }
    return fVal;
}

uint16_t AdcDrv_ReadRing(uint8_t u8Id, float *pfBuf, uint16_t u16Max)
{
    if (u8Id >= s_u8InstNum || pfBuf == NULL || u16Max == 0U) return 0U;
    adc_drv_inst_t *pstc = &s_pstcInsts[u8Id];
    uint16_t u16N = (pstc->u16Count < u16Max) ? pstc->u16Count : u16Max;
    /* 从最新写入位置往前取 u16N 个（环形） */
    for (uint16_t i = 0U; i < u16N; i++) {
        uint16_t idx = (uint16_t)((pstc->u16WriteIdx + ADC_DRV_RING_SIZE - 1U - i) % ADC_DRV_RING_SIZE);
        pfBuf[i] = pstc->afRing[idx];
    }
    return u16N;
}

/* ==================== dev_adc_ops 接口表实现（rt_pin_ops 风格） ==================== */

/* 通用通道号 → DDL 通道/端口/引脚 映射 */
typedef struct {
    uint8_t  channel;   /* 与 dev_adc_ch_cfg_t.channel 对应 */
    uint8_t  ddl_ch;    /* DDL 通道枚举（ADC_CH4 等） */
    uint8_t  port;
    uint16_t pin;
} hc32_adc_ch_map_t;

/* 通道绑定宏见 hc32_drv_adc.h（换板/换引脚只改 .h，勿在此写字面量） */
static const hc32_adc_ch_map_t s_hc32_adc_ch_map[] = {
    { ADC_DRV_VOLT_CH, ADC_DRV_VOLT_DDL_CH, ADC_DRV_VOLT_PORT, ADC_DRV_VOLT_PIN },  /* 电压 */
    { ADC_DRV_CUR_CH,  ADC_DRV_CUR_DDL_CH,  ADC_DRV_CUR_PORT,  ADC_DRV_CUR_PIN  },  /* 电流 */
};

static adc_drv_inst_t s_hc32_insts[HC32_ADC_MAX_INST];
static adc_drv_ch_t   s_hc32_chs[HC32_ADC_MAX_INST];

static int hc32_adc_init(const dev_adc_ch_cfg_t *table, uint8_t num)
{
    if (table == NULL || num == 0U || num > HC32_ADC_MAX_INST) {
        return -1;
    }

    for (uint8_t i = 0U; i < num; i++) {
        const hc32_adc_ch_map_t *pMap = NULL;
        for (uint8_t j = 0U; j < (uint8_t)(sizeof(s_hc32_adc_ch_map) / sizeof(s_hc32_adc_ch_map[0])); j++) {
            if (s_hc32_adc_ch_map[j].channel == table[i].channel) {
                pMap = &s_hc32_adc_ch_map[j];
                break;
            }
        }
        if (pMap == NULL) {
            return -2;   /* 未支持的通道 */
        }
        s_hc32_chs[i].u8Channel   = pMap->ddl_ch;
        s_hc32_chs[i].u8Port      = pMap->port;
        s_hc32_chs[i].u16Pin      = pMap->pin;
        s_hc32_chs[i].fGain       = table[i].gain;
        s_hc32_chs[i].fOffset     = table[i].offset;
        s_hc32_chs[i].bAbs        = table[i].abs;
        s_hc32_chs[i].pfnCallback = NULL;
    }
    return AdcDrv_Init(s_hc32_chs, num, s_hc32_insts);
}

static void hc32_adc_start(void)          { AdcDrv_Start(); }
static void hc32_adc_stop(void)           { AdcDrv_Stop(); }

static int hc32_adc_get_latest(uint8_t id, float *val)
{
    if (val == NULL) return -1;
    *val = AdcDrv_GetLatest(id);
    return 0;
}

static int hc32_adc_get_raw(uint8_t id, uint16_t *raw)
{
    if (raw == NULL) return -1;
    *raw = AdcDrv_GetLatestRaw(id);
    return 0;
}

static uint16_t hc32_adc_read_ring(uint8_t id, float *buf, uint16_t max)
{
    return AdcDrv_ReadRing(id, buf, max);
}

static int hc32_adc_get_mean(uint8_t id, float *val)
{
    if (val == NULL) return -1;
    *val = AdcDrv_GetMean(id);
    return 0;
}

/* 暴露给 Dev 层绑定的接口表 */
const struct dev_adc_ops hc32_adc_ops = {
    hc32_adc_init,
    hc32_adc_start,
    hc32_adc_stop,
    hc32_adc_get_latest,
    hc32_adc_get_raw,
    hc32_adc_read_ring,
    hc32_adc_get_mean,
};

/* EOF */
