/**
 * @file    dev_adc.c
 * @brief   ADC 设备（Dev 层）：通道配置表 + 接口表调用（注册由 Dev_RegisterAll 集中管理）
 * @note    底层驱动通过 dev_adc_ops 注入，Dev 层不依赖具体芯片；
 *          电压/电流通道同一 SEQ_A（时间对齐；通道→引脚绑定宏见 Adp/hc32_drv_adc.h）；
 *          采样由 TMR0_1 硬件触发 500us（2kHz），EOCA 中断维护环形缓冲 + 滑动平均窗口；
 *          1ms 检测 ISR 经 Dev_Adc_GetMean 读 10ms 滑动均值。
 */
#include "dev_adc.h"
#include "dev_adc_ops.h"
#include "hc32_drv_adc.h"   /* ADC_DRV_VOLT_CH / ADC_DRV_CUR_CH：通道号与引脚绑定单点维护 */
#include <stddef.h>

/* 全局接口表指针（由 Dev_Adc_Bind 注入） */
static const struct dev_adc_ops *g_adc_ops = NULL;

/* 通道配置表（只含换算；通道号/端口/引脚绑定宏在 Adp/hc32_drv_adc.h，
   id 序 = 表序：id0=电压、id1=电流，Dev_Adc_GetLatest 按此取值，勿换行序） */
static const dev_adc_ch_cfg_t s_astcChTable[] = {
    { ADC_DRV_VOLT_CH,  31.3f, 0.0f, false },   /* 电压：gain=31.303 = 100k:3.3k 分压 */
    { ADC_DRV_CUR_CH,    1.0f, 0.0f, false },   /* 电流：gain=1，ADC 层仅出电压 V，V→mA 换算在 dev_cur_sensor */
};


void Dev_Adc_Bind(const struct dev_adc_ops *ops)
{
    g_adc_ops = ops;
}


void Dev_Adc_Init(void)
{
    if (g_adc_ops != NULL && g_adc_ops->init != NULL) {
        (void)g_adc_ops->init(s_astcChTable,
                              (uint8_t)(sizeof(s_astcChTable) / sizeof(s_astcChTable[0])));
    }
}

void Dev_Adc_Start(void)
{
    if (g_adc_ops != NULL && g_adc_ops->start != NULL) {
        g_adc_ops->start();
    }
}

void Dev_Adc_Stop(void)
{
    if (g_adc_ops != NULL && g_adc_ops->stop != NULL) {
        g_adc_ops->stop();
    }
}

int Dev_Adc_GetLatest(float *pfVolt, float *pfCurr)
{
    if (pfVolt == NULL || pfCurr == NULL ||
        g_adc_ops == NULL || g_adc_ops->get_latest == NULL) {
        return -1;
    }
    (void)g_adc_ops->get_latest(0, pfVolt);
    (void)g_adc_ops->get_latest(1, pfCurr);
    return 0;
}

uint16_t Dev_Adc_ReadRing(uint8_t id, float *pfBuf, uint16_t u16Max)
{
    if (pfBuf == NULL || u16Max == 0U ||
        g_adc_ops == NULL || g_adc_ops->read_ring == NULL) {
        return 0U;
    }
    return g_adc_ops->read_ring(id, pfBuf, u16Max);
}

int Dev_Adc_GetRaw(uint8_t id, uint16_t *pu16Raw)
{
    if (pu16Raw == NULL || g_adc_ops == NULL || g_adc_ops->get_raw == NULL) {
        return -1;
    }
    return g_adc_ops->get_raw(id, pu16Raw);
}

int Dev_Adc_GetMean(uint8_t id, float *pfVal)
{
    if (pfVal == NULL || g_adc_ops == NULL || g_adc_ops->get_mean == NULL) {
        return -1;
    }
    return g_adc_ops->get_mean(id, pfVal);
}

/* EOF */







