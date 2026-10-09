/**
 * @file    dev_hall_motor.h
 * @brief   电机霍尔设备：双霍尔边沿计数 + B 下降沿采样 A 判向 + RPM 测速链
 *          + stalled/is_running/HALL_STATUS 检测（完整移植参考工程 Motor_hall）
 * @note    A 型执行模型：EXTI 双边沿 ISR 计数；rod_task 10ms 调 MotorHall_Task()；
 *          全部检测量仅作查询观测量，不接错误/急停链路（堵转后续由过流实现）。
 */
#ifndef __DEV_HALL_MOTOR_H__
#define __DEV_HALL_MOTOR_H__

#include <stdint.h>
#include "hc32_ll.h"      /* GPIO_PORT_C / EXTINT_CH13 / INT_SRC_PORT_EIRQ13 等配置宏 */

/* ============ 配置宏（统一放头文件，规范 §11） ============ */
#define MOTOR_HALL_MAX_AXIS_NUM     (2U)    /* 与 MAX_AXIS_NUM(dev_model.h) 保持一致 */

/* GPIO：PC13(A) / PC14(B)，新板接线（旧板 PA9/PA10） */
#define MOTOR_HALL_A_PORT           GPIO_PORT_C
#define MOTOR_HALL_A_PIN            GPIO_PIN_13
#define MOTOR_HALL_B_PORT           GPIO_PORT_C
#define MOTOR_HALL_B_PIN            GPIO_PIN_14

/* EXTI：CH13/CH14 双边沿，INT013/INT014，EIRQ13/EIRQ14 */
#define MOTOR_HALL_A_EIRQ_CH        EXTINT_CH13
#define MOTOR_HALL_B_EIRQ_CH        EXTINT_CH14
#define MOTOR_HALL_A_IRQN           INT013_IRQn
#define MOTOR_HALL_B_IRQN           INT014_IRQn
#define MOTOR_HALL_A_IRQ_SRC        INT_SRC_PORT_EIRQ13
#define MOTOR_HALL_B_IRQ_SRC        INT_SRC_PORT_EIRQ14
#define MOTOR_HALL_IRQ_PRIORITY     DDL_IRQ_PRIO_02

/* 电机参数：3 极对 × 2 霍尔 × 2 边沿 = 12 边沿/机械转 */
#define MOTOR_HALL_POLE_PAIRS       (3U)
#define MOTOR_HALL_HALL_COUNT       (2U)
#define MOTOR_HALL_CUSTOM_PULSES_PER_REV  (0U)   /* 0=按极对×霍尔数自动算 */

/* A/B 接线反接时置 1（运行时 Watch 可改 g_mothall_invert_dir） */
#define MOTOR_HALL_DIRECTION_INVERT_DEFAULT  (0U)

/* 测量参数（与参考 Motor_hall.c 完全一致） */
#define MOTOR_HALL_MIN_PULSE_INTERVAL_US   (50U)
#define MOTOR_HALL_MAX_PULSE_INTERVAL_US   (200000U)
#define MOTOR_HALL_STOP_DETECTION_MS       (50U)
#define MOTOR_HALL_STALL_DETECTION_MS      (500U)
#define MOTOR_HALL_RPM_WINDOW_SIZE         (6U)
#define MOTOR_HALL_DIRECTION_CONFIRM_CNT   (3U)
#define MOTOR_HALL_CHECK_INTERVAL_MS       (1000U)
#define MOTOR_HALL_WARNING_THRESHOLD       (10U)   /* A/B 计数差百分比 */
#define MOTOR_HALL_TASK_PERIOD_MS          (10U)   /* Task 测速周期 */
#define MOTOR_HALL_PRINT_INTERVAL_MS       (2000U)

/* 方向（对应参考 motor_direction_t） */
typedef enum {
    MOTOR_HALL_DIR_NONE = 0,
    MOTOR_HALL_DIR_FORWARD,
    MOTOR_HALL_DIR_REVERSE,
    MOTOR_HALL_DIR_STOP,
} MotorHallDir_t;

/* 双霍尔工作状态（对应参考 hall_working_status_t） */
typedef enum {
    MOTOR_HALL_STATUS_NONE = 0,
    MOTOR_HALL_STATUS_A_ONLY,
    MOTOR_HALL_STATUS_B_ONLY,
    MOTOR_HALL_STATUS_BOTH,
    MOTOR_HALL_STATUS_ERROR,
} MotorHallStatus_t;

/* 运行时反转开关（ISR 读取） */
extern volatile uint8_t g_mothall_invert_dir;

void     MotorHall_Init(void);      /* 首次注册 EXTI；IDLE 重入仅复位业务态 */
void     MotorHall_Task(void);      /* 10ms：测速/堵转观测/霍尔状态/增量累积 */

int32_t  MotorHall_GetDeltaPulses(uint8_t axis_id);   /* 读清带符号增量 */
float    MotorHall_GetRpm(uint8_t axis_id);
float    MotorHall_GetRpmRaw(uint8_t axis_id);
uint32_t MotorHall_GetPulseIntervalUs(uint8_t axis_id);
MotorHallDir_t MotorHall_GetDirection(uint8_t axis_id);
uint8_t  MotorHall_GetDirectionConfidence(uint8_t axis_id);
uint8_t  MotorHall_IsDirectionChanged(uint8_t axis_id);   /* 读清 */
uint32_t MotorHall_GetHallACount(uint8_t axis_id);
uint32_t MotorHall_GetHallBCount(uint8_t axis_id);
uint32_t MotorHall_GetTotalPulseCount(uint8_t axis_id);
void     MotorHall_ResetCounts(uint8_t axis_id);
MotorHallStatus_t MotorHall_GetStatus(uint8_t axis_id);
uint8_t  MotorHall_GetActiveHallCount(uint8_t axis_id);
uint16_t MotorHall_GetPulsesPerRev(uint8_t axis_id);
uint8_t  MotorHall_IsRunning(uint8_t axis_id);
uint8_t  MotorHall_IsStalled(uint8_t axis_id);

#endif /* __DEV_HALL_MOTOR_H__ */
