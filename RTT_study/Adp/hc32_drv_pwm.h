/**
 * @file    hc32_drv_pwm.h
 * @brief   电机 PWM 硬件适配：现行实现 TMR4_3 四输出（PB9~PB6, func2）；
 *          原 TMRA4 实现保留（PWM_DRV_USE_TMR4=0 切回，移植自 hkb_1 adapter_pwm）
 * @note    TMR4 引脚映射（与参考工程 dev_motor 的 CH1/2、CH3/4 分组一致）：
 *          PB9 = TIM4_3_OUH（相U高）  PB8 = TIM4_3_OUL（相U低）  → 组 U
 *          PB7 = TIM4_3_OVH（相V高）  PB6 = TIM4_3_OVL（相V低）  → 组 V
 *          极性语义（对齐参考 dev_motor）：
 *          运行态 = 低有效（组内 H/L 同相，POCR=HOLD/HOLD；正转 V=D/U=100-D，反转互换）；
 *          停止态 = 混合极性（组内 L 反相，POCR=HOLD/INVT，两组均 50% → 端压差≈0）；
 *          OCCR/OCMR 支持 peak 时刻缓冲装载，运行中改比较值无毛刺；
 *          OC 关断（OC_Cmd DISABLE）输出恒低 = 安全态（duty=0 用它表达，不走比较值 0）。
 */
#ifndef __HC32_DRV_PWM_H__
#define __HC32_DRV_PWM_H__

/* 实现选择：1 = TMR4_3（现行）；0 = TMRA4（旧实现保留） */
#ifndef PWM_DRV_USE_TMR4
#define PWM_DRV_USE_TMR4        1
#endif

/* 运行占空比限幅（参考 dev_motor：MOTOR_DUTY_MIN/MAX） */
#define PWM_DUTY_MIN            (2U)
#define PWM_DUTY_MAX            (98U)
#define PWM_DUTY_STOP           (50U)       /* 停止态混合极性占空比 */
#define PWM_FREQ_DFT_HZ         (6500U)     /* 默认 PWM 频率（沿用原配置） */

#if PWM_DRV_USE_TMR4

#include <stdint.h>
#include "hc32_ll_tmr4.h"
#include "hc32_ll_gpio.h"

/* ===================== TMR4_3 硬件绑定（换芯片/换引脚只改这里） ===================== */
#define PWM_TMR4_UNIT           (CM_TMR4_3)
#define PWM_TMR4_CLK            (FCG2_PERIPH_TMR4_3)
#define PWM_GPIO_FUNC_TMR4      (GPIO_FUNC_2)   /* PB6~PB9 复用功能码 2 = TIM4_3 */

/* 输出引脚绑定（换板只改这里；与下方 OC/组宏的对应关系见行尾注释） */
#define PWM_OUH_PORT            (GPIO_PORT_B)   /* PB9 = TIM4_3_OUH（相U高） */
#define PWM_OUH_PIN             (GPIO_PIN_09)
#define PWM_OUL_PORT            (GPIO_PORT_B)   /* PB8 = TIM4_3_OUL（相U低） */
#define PWM_OUL_PIN             (GPIO_PIN_08)
#define PWM_OVH_PORT            (GPIO_PORT_B)   /* PB7 = TIM4_3_OVH（相V高） */
#define PWM_OVH_PIN             (GPIO_PIN_07)
#define PWM_OVL_PORT            (GPIO_PORT_B)   /* PB6 = TIM4_3_OVL（相V低） */
#define PWM_OVL_PIN             (GPIO_PIN_06)

/* 输出组：极性（POCR）接口按耦合通道寻址（U/V 各管一对 H/L） */
#define PWM_GRP_U               ((uint8_t)TMR4_PWM_CH_U)    /* OUH=PB9 / OUL=PB8 */
#define PWM_GRP_V               ((uint8_t)TMR4_PWM_CH_V)    /* OVH=PB7 / OVL=PB6 */

/* OC 单通道：比较值/OCMR/使能接口按单通道寻址（UH=0 UL=1 VH=2 VL=3，奇偶=低/高通道） */
#define PWM_OC_UH               ((uint8_t)TMR4_OC_CH_UH)    /* PB9 PHU */
#define PWM_OC_UL               ((uint8_t)TMR4_OC_CH_UL)    /* PB8 PLU */
#define PWM_OC_VH               ((uint8_t)TMR4_OC_CH_VH)    /* PB7 PHV */
#define PWM_OC_VL               ((uint8_t)TMR4_OC_CH_VL)    /* PB6 PLV */

/* ===================== TMR4 接口 ===================== */
/* 初始化：时钟 + 4 引脚 func2 + 基础计数器 + 4 通道 OC + U/V 组 POCR + 启动；
   上电安全态 = 停止态（混合极性 50%，对齐参考工程 first-stop 行为） */
void     PwmHw4_Init(uint32_t freq_hz);
uint16_t PwmHw4_GetPeriod(void);
/* 占空比% → 比较值（低有效：低电平占比 = duty） */
uint16_t PwmHw4_DutyToCompare(uint32_t duty_pct);
/* 写比较值（组内 H/L 同值同写；OCCR peak 时刻缓冲装载，无毛刺。
   L 通道波形依赖 H/L 同一计数点同时匹配，两者比较值必须恒等） */
void     PwmHw4_SetCompare(uint8_t grp, uint16_t cmp);
/* 极性切换（POCR LVLS 位读改写，原子）：运行=H/L 同相；停止=L 反相（混合极性） */
void     PwmHw4_SetRunPolarity(uint8_t grp);
void     PwmHw4_SetStopPolarity(uint8_t grp);
/* OC 输出使能/关断：关断 = 恒低（安全态），比比较值 0 更可靠 */
void     PwmHw4_OutputCmd(uint8_t grp, en_functional_state_t enNewState);

#else /* ===================== 旧 TMRA4 实现（保留） ===================== */

#include <stdint.h>
#include "hc32_ll_tmra.h"
#include "hc32_ll_gpio.h"

#define PWM_TMRA_CLK_HZ     (100000000UL)   /* TMRA4 计数时钟 = PCLK1（官方 Readme：TimerA 钟源是 PCLK1；示波器实测 80us/7692 反推吻合） */
#define PWM_FREQ_MIN_HZ     (10000U)        /* 运行中改频下限（仅 PwmHw_SetFreq 用） */
#define PWM_FREQ_MAX_HZ     (20000U)        /* 运行中改频上限 */
#define PWM_GPIO_FUNC_TIMA  (4U)            /* PB7/PB9 复用功能码 4 = TMRA（旧 DDL Func_Tima0 同值） */

/* ===================== 电机 PWM 引脚/通道硬件绑定（hkb_1 hardware.h 同配置；换芯片改这里） ===================== */
#define PWM_PHU_PORT        GPIO_PORT_B     /* PHU = PB9 */
#define PWM_PHU_PINS        GPIO_PIN_09
#define PWM_PHU_TMRA        CM_TMRA_4
#define PWM_PHU_CH          TMRA_CH4

#define PWM_PHV_PORT        GPIO_PORT_B     /* PHV = PB7 */
#define PWM_PHV_PINS        GPIO_PIN_07
#define PWM_PHV_TMRA        CM_TMRA_4
#define PWM_PHV_CH          TMRA_CH2

#define PWM_MOTOR_LOW_ACTIVE 1U             /* 1=低有效：占空比 = 输出低电平时间百分比（hkb_1 实测配置） */

/* 默认周期值（= PWM_TMRA_CLK_HZ / PWM_FREQ_DFT_HZ - 1；改频率请用 PwmHw 后续接口，此处仅初值） */
#define PWM_PERIOD_DFT      ((PWM_TMRA_CLK_HZ / PWM_FREQ_DFT_HZ) - 1U)

/* 通道初始化：引脚复用 + TMRA 基础配置 + PWM 极性/比较值 + 输出使能 + 启动 */
void PwmHw_ChannelInit(uint8_t u8Port, uint16_t u16Pin, CM_TMRA_TypeDef *TMRAx,
                       uint32_t u32Ch, float fInitDutyPct, uint8_t u8LowActive);
void PwmHw_SetDutyPct(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch, float fDutyPct);
void PwmHw_SetCompareValue(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch, uint32_t u32CmpVal);
void PwmHw_SetForcePolarity(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch, uint16_t u16Polarity);
void PwmHw_CompareEnable(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch);
void PwmHw_CompareDisable(CM_TMRA_TypeDef *TMRAx, uint32_t u32Ch);
/* 诊断：实测 TMRA 计数时钟（100us 窗口采样反推，Hz） */
uint32_t PwmHw_ProbeCountClock(CM_TMRA_TypeDef *tmra, uint32_t period);

#endif /* PWM_DRV_USE_TMR4 */

#endif /* __HC32_DRV_PWM_H__ */
