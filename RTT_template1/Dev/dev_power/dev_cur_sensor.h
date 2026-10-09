#ifndef __DEV_CUR_SENSOR_H__
#define __DEV_CUR_SENSOR_H__

#include <stdint.h>

/* 阈值默认值宏（CUR_*_DFT）已集中至 Dev/dev_param/dev_param.h（存储默认值单点） */

/* ===================== 差分放大器换算（0V 零点；总灵敏度=采样电阻×放大倍数） ===================== */
/* ADC 层 CH6 只输出电压 V，本模块把 V→mA 换算下沉至此（换传感器只改这里） */
#define CUR_SENSOR_ZERO_V        (0.0f)       /* 零点电压 V（差分放大器 0A 时输出 ≈0V） */
#define CUR_AMP_GAIN             (20.0f)      /* 差分放大器倍数（硬件标称 20 倍） */
#define CUR_SHUNT_MOHM           (10.0f)      /* 采样电阻 mΩ（标称值） */
/* 标称灵敏度：1e6/(10×20) = 5000 mA/V（=200mV/A）；实测电流显示偏大 10 倍 →
   等效灵敏度 500 mA/V（等效增益 200 或等效采样电阻 100mΩ，待硬件确认原因），
   校准系数 /10 并入公式 */
#define CUR_SENSITIVITY_MA_PER_V (1000.0f * 1000.0f / (CUR_SHUNT_MOHM * CUR_AMP_GAIN) / 10.0f)

/* ===================== 模拟模式 ===================== */
/* 1=模拟：1ms 检测不读 ADC，改用 g_cur_sim_ma 直接赋值（表达式窗口实时可改，单位 mA） */
#define CUR_SIM_MODE_EN         (0)
extern volatile uint32_t g_cur_sim_ma;    /* 模拟电流 mA（初值见 .c，可实时改） */

/* 阈值配置（RAM 变量，debugger 可实时改；默认见 .c） */
typedef struct {
    float    over_th_ma;   /* 过流阈值 mA */
    uint16_t window_ms;   /* 过流判定窗口 ms */
    uint16_t block_ms;    /* 方向变化过流屏蔽 ms：停止→正/反转、换向浪涌期不判定（0=不屏蔽） */
} CurCfg_t;
extern volatile CurCfg_t g_cur_cfg;

void CurrentSensor_Init(void);
void CurrentSensor_Isr1ms(void); /* 1ms ISR 检测（TMR0_2 心跳调用） */
void CurrentSensor_ReqBlock(uint16_t block_ms);  /* 请求屏蔽过流检测 block_ms（GPIO 输出方向变化沿调用，覆盖式置位） */
void CurrentSensor_GetInfo(float *pfCurr_mA, uint8_t *pu8Status);  /* 0=正常 1=过流 */
uint16_t CurrentSensor_GetOverMs(void);                    /* 当前超阈值累计时间（ms），观测用 */
float    CurrentSensor_GetFaultMa(void);                   /* 故障触发瞬间的电流快照 mA（sys_sm 打印用） */

#endif /* __DEV_CUR_SENSOR_H__ */










