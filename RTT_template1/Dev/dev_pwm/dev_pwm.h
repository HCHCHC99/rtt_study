/**
 * @file    dev_pwm.h
 * @brief   电机 PWM 输出设备：TMR4_3 双组 H 桥驱动 + 缓启动状态机（专用调速线程）
 * @note    仲裁输出接缝：Dev_Pwm_Init 里 Arb_BindOutputOps 绑定 fwd/rev/stop；
 *          ops 只写目标（方向+占空比）+ 信号量，缓启动 ramp 由 pwm 调速线程推进。
 *          占空比语义（低有效，对齐参考 dev_motor）：
 *          fwd: V 组=eff / U 组=100-eff；rev: 互换；停止: U=V=50 + 混合极性。
 *          极性仅在 U=V=50%（端压差=0）时刻切换，电流连续无突变。
 */
#ifndef __DEV_PWM_H__
#define __DEV_PWM_H__

#include <stdint.h>

/* ===================== 设备级可调项（硬件无关） ===================== */
#define PWM_RUN_DUTY_PCT        (20U)   /* 默认有效占空比%（ops duty_pct=0 时使用） */
#define PWM_RAMP_STEP_PCT       (2U)    /* 运行 ramp 步长 %/10ms（2% → 50→98 约 240ms） */
#define PWM_RAMP_STEP_STOP_PCT  (10U)   /* 停止 ramp 步长 %/10ms（10% → 98→50 约 50ms） */
#define PWM_TICK_MS             (10U)   /* 调速线程 tick 周期 */

/* ===================== 命令接缝方向值（act/ISR 写 → 调速线程读） ===================== */
#define PWM_CMD_DIR_STOP        (0U)    /* 停止：U=V=50 + 混合极性 */
#define PWM_CMD_DIR_FWD         (1U)    /* 正转（伸出） */
#define PWM_CMD_DIR_REV         (2U)    /* 反转（缩回） */

void Dev_Pwm_Init(void);       /* 注册表 init：硬件初始化 + 调速线程 + 绑定仲裁输出 */
void Dev_Pwm_Task(void);       /* 兼容预留（ramp 已内化到 pwm 调速线程，本函数为空） */
int  Dev_PwmMotor_RunFwd(void);    /* 正转（伸出），默认占空比 */
int  Dev_PwmMotor_RunRev(void);    /* 反转（缩回），默认占空比 */
int  Dev_PwmMotor_Stop(void);      /* 停止：快斜坡回 50/50 → 混合极性 */
int  Dev_PwmMotor_EStop(void);     /* 急停：跳过 ramp 立即混合极性 50/50（ISR 上下文亦可调） */

#endif /* __DEV_PWM_H__ */
