/**
 * @file    dev_adc.h
 * @brief   ADC 设备（Dev 层）：通过 dev_adc_ops 接口表访问底层 ADC 驱动
 */
#ifndef __DEV_ADC_H__
#define __DEV_ADC_H__

#include <stdint.h>
#include "dev_adc_ops.h"

/* 绑定底层 ADC 驱动接口表（如 hc32_adc_ops） */
void Dev_Adc_Bind(const struct dev_adc_ops *ops);

void Dev_Adc_Init(void);      /* 注册表 init：经接口表初始化 */
void Dev_Adc_Start(void);     /* 启动采样：TMR0_1 硬件触发 500us（2kHz） */
void Dev_Adc_Stop(void);      /* 停止采样 */
int  Dev_Adc_GetLatest(float *pfVolt, float *pfCurr);                 /* 上层轮询：最近一次工程值（pfVolt/pfCurr 均为通道电压 V；mA 换算在 dev_cur_sensor） */
uint16_t Dev_Adc_ReadRing(uint8_t id, float *pfBuf, uint16_t u16Max); /* 读环形缓冲（换算后工程值，电压通道 V / 电流通道 V） */
int      Dev_Adc_GetRaw(uint8_t id, uint16_t *pu16Raw);               /* 最近一次原始 AD 值 */
int      Dev_Adc_GetMean(uint8_t id, float *pfVal);                   /* 10ms 滑动窗口平均（电压通道 V 含分压换算；电流通道 V 无换算，mA 见 dev_cur_sensor） */

#endif /* __DEV_ADC_H__ */





