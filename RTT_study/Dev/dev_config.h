/**
 * @file    dev_config.h
 * @brief   设备层集中配置：功能开关 + 优先级常量
 * @note    由 Dev_RegisterAll() 使用；改这里即可裁剪/调整设备
 */
#ifndef __DEV_CONFIG_H__
#define __DEV_CONFIG_H__

/* ============ 设备功能开关（1=启用，0=裁剪） ============ */
#define DEV_ENABLE_ADC              1   /* ADC 设备（经 dev_adc_ops 访问底层驱动） */
#define DEV_ENABLE_CUR_SENSOR       1   /* 电流传感器设备 */
#define DEV_ENABLE_BUS_VOLTAGE      1   /* 母线电压设备 */
#define DEV_ENABLE_POLARITY         1   /* 电源极性设备（GPIO 双窗口，1ms ISR） */
#define DEV_ENABLE_MONITOR          1   /* 系统观测模块（Watch 用全局 g_monitor） */
#define DEV_ENABLE_ACT_ARB          1   /* 电机仲裁模块（C 模式：rt_mq + 每轴互斥量 + 仲裁线程） */
#define DEV_ENABLE_ARB_SELFTEST     0   /* 仲裁台架自测线程（已关闭：输入源由用户经模拟极性自行驱动） */
#define DEV_ENABLE_MOTOR_GPIO       1   /* 电机 GPIO 输出设备（新板：PB8/PB9 双脚方向控制，双脚高有效） */
#define DEV_ENABLE_HALL_MOTOR       1   /* 电机霍尔设备（EXTI 计数 + 测速，观测用） */
#define DEV_ENABLE_PARAM            1   /* 应用参数管理（Flash 掉电保存 g_volt_cfg/g_cur_cfg，main 上电调 Dev_Param_Init） */

/* ============ 设备优先级常量（数字越小优先级越高，与 dev_registry 语义一致） ============ */
#define DEV_PRIO_HIGH               2
#define DEV_PRIO_MID                3
#define DEV_PRIO_LOW                4
#define DEV_PRIO_MONITOR            DEV_PRIO_LOW

#endif /* __DEV_CONFIG_H__ */


