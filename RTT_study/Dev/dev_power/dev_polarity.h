/**
 * @file    dev_polarity.h
 * @brief   电源极性设备：单管方向检测 + 滑窗消抖 + 状态判定 + 跳变发事件（Task/di_task 2ms 调用）
 * @note    硬件方案：一只三极管检测电源方向，PB15 输入（板上拉电阻）：
 *          24V 正向接法 = 管子导通 = PB15 低电平；反向接法 = 管子截止 = PB15 高电平。
 *          与旧双管方案（PC14/PC15 P/N 各一管）相比：无 UNPOWERED/ABNORMAL 检测能力
 *          （调试不接 24V 时 PB15 恒高 = REV 态；掉电兜底由电压欠压检测承担）。
 *          5 点窗口（2ms 采样 => 10ms 消抖）；仅在稳定状态跳变沿发事件：
 *          轴事件 EVT_ACT_POLARITY_FWD/REV + 系统事件 EVT_SYS_POLARITY_CHG（换向清除决策在 dev_state）。
 */
#ifndef __DEV_POLARITY_H__
#define __DEV_POLARITY_H__

#include <stdint.h>

/* 引脚配置（统一放头文件；换板只改这里） */
#define POWER_DIR_PIN       GET_PIN(B, 15)   /* 方向检测（单管+上拉）：0=FWD 正向 1=REV 反向 */

/* 默认配置宏（统一放头文件） */
#define POLARITY_WIN_SIZE   (5U)             /* 窗口 5 点：di_task 2ms 采样 => 10ms 消抖 */

/* 仲裁命令映射（当前唯一实体电机轴；axis 1 预留） */
#define POLARITY_ARB_AXIS_ID        (0U)
#define POLARITY_ARB_RUN_DUTY_PCT   (98U)   /* 后续按电机负载和温升调整 */

/* 电源极性状态（单管方案仅 FWD/REV 可达；UNKNOWN=窗口未满保持上次态）。
   UNPOWERED/ABNORMAL 枚举保留：兼容事件消费方 + 回退双管方案时零改动 */
typedef enum {
    POLARITY_UNKNOWN = 0,   /* 窗口未满 / 不稳定：保持上次稳定态，不发事件 */
    POLARITY_UNPOWERED,     /* （保留位，单管方案不可达）旧双管：P=0 N=0 */
    POLARITY_FWD,           /* 正向：PB15=0 */
    POLARITY_REV,           /* 反向：PB15=1 */
    POLARITY_ABNORMAL,      /* （保留位，单管方案不可达）旧双管：P=1 N=1 */
} PolarityState_t;

/* ===================== 模拟模式 ===================== */
/* 1=模拟：Polarity_Scan 不读 GPIO/不做窗口消抖，改用 g_pol_sim_state 直接判定
   （表达式窗口实时可改：0=UNKNOWN 保持上次 1=UNPOWERED 2=FWD 3=REV 4=ABNORMAL）
   需要无 24V 的"停止态"调试时切回 1；上板实测方向必须为 0 */
#define POLARITY_SIM_MODE_EN    (0)
extern volatile PolarityState_t g_pol_sim_state;   /* 模拟极性状态（含义见 PolarityState_t；UNKNOWN=保持不发事件） */

void Polarity_Init(void);              /* 注册表 init：复位窗口与状态 */
void Polarity_Scan(void);              /* 扫描（di_task 2ms 调）：读 GPIO + 推窗 + 判定 + 跳变发事件 */
PolarityState_t Polarity_GetState(void); /* 查询上次稳定状态（电机控制/监控/换向清除用） */
void Polarity_PrintPending(void);    /* 线程上下文：打印未处理的极性跳变（调试，走 POLARITY_PRINT） */

#endif /* __DEV_POLARITY_H__ */
