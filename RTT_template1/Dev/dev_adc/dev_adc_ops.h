/**
 * @file    dev_adc_ops.h
 * @brief   ADC 设备接口表（rt_pin_ops 风格：Dev 层只认接口，实现由具体芯片提供）
 */
#ifndef __DEV_ADC_OPS_H__
#define __DEV_ADC_OPS_H__

#include <stdint.h>
#include <stdbool.h>

/* 滑动平均窗口时长 ms（芯片无关语义；点数由实现层按采样间隔换算，见 Adp/hc32_drv_adc.h） */
#define ADC_MEAN_WINDOW_MS          (10U)

/* 通用 ADC 通道配置（芯片无关，只含换算；端口/引脚由实现层映射） */
typedef struct {
    uint8_t channel;    /* 通道号（HC32 为 ADC_CH4 / ADC_CH6） */
    float   gain;       /* 换算增益 */
    float   offset;     /* 换算偏移 */
    bool    abs;        /* 取绝对值（电流通道 true） */
} dev_adc_ch_cfg_t;

/* ADC 设备接口表（rt_pin_ops 风格） */
struct dev_adc_ops {
    int  (*init)(const dev_adc_ch_cfg_t *table, uint8_t num); /* 通道配置 + 实例初始化 */
    void (*start)(void);   /* 启动采样：TMR0_1 硬件触发（500us） */
    void (*stop)(void);    /* 停止采样 */
    int  (*get_latest)(uint8_t id, float *val);                /* 最近一次工程值 */
    int  (*get_raw)(uint8_t id, uint16_t *raw);                   /* 最近一次原始 AD 值 */
    uint16_t (*read_ring)(uint8_t id, float *buf, uint16_t max); /* 读环形缓冲 */
    int  (*get_mean)(uint8_t id, float *val);                  /* 滑动窗口平均（换算后工程值） */
};

#endif /* __DEV_ADC_OPS_H__ */




