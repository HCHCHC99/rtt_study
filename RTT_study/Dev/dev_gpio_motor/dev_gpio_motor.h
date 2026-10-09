/**
 * @file    dev_gpio_motor.h
 * @brief   电机 GPIO 输出设备（新板）：双脚方向控制，替代 TMR4 PWM 调速
 * @note    真值表（高电平有效，两脚成对写 + 关中断同步翻转）：
 *          停转/急停：FWD=1 REV=1（双高 = 刹车态，用户实测确认）
 *          正转(伸出)：FWD=1 REV=0      反转(缩回)：FWD=0 REV=1
 *          仲裁接缝：Init 里 Arb_BindOutputOps 绑定 fwd/rev/stop——上层链
 *          （仲裁/rod 状态机/sys_sm/过流软限位）零改动。
 *          无调速/ramp/线程：任意非零 duty = 全速输出；EStop 立即双高刹车（ISR 可调）。
 */
#ifndef __DEV_GPIO_MOTOR_H__
#define __DEV_GPIO_MOTOR_H__

#include <stdint.h>
#include "hc32_ll.h"    /* GPIO_PORT_B / GPIO_PIN_08 等绑定宏 */

/* ============ 引脚绑定宏（换板/换引脚只改这里） ============ */
#define MOTOR_GPIO_FWD_PORT     (GPIO_PORT_B)   /* PB8 = PL1：正转(伸出) 高有效 */
#define MOTOR_GPIO_FWD_PIN      (GPIO_PIN_08)
#define MOTOR_GPIO_REV_PORT     (GPIO_PORT_B)   /* PB9 = PH1：反转(缩回) 高有效 */
#define MOTOR_GPIO_REV_PIN      (GPIO_PIN_09)

/* ============ API（fwd/rev/stop + EStop） ============ */
void Dev_MotorGpio_Init(void);    /* 注册表 init：两脚输出高（安全态=刹车）+ 绑定仲裁输出 ops */
int  Dev_MotorGpio_RunFwd(void);  /* 正转（伸出） */
int  Dev_MotorGpio_RunRev(void);  /* 反转（缩回） */
int  Dev_MotorGpio_Stop(void);    /* 停止：双高刹车 */
int  Dev_MotorGpio_EStop(void);   /* 急停：立即双高刹车（ISR 上下文亦可调） */

/* 电机输出相序反置：1=交换 FWD/REV 输出（Flash A 块 motor_dir_seq 上电应用；
   本 Init 不复位该值，掉电保持由 param 侧负责） */
void Dev_MotorGpio_SetDirInvert(uint8_t invert);

#endif /* __DEV_GPIO_MOTOR_H__ */
