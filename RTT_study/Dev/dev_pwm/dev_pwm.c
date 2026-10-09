/**
 * @file    dev_pwm.c
 * @brief   电机 PWM 输出设备实现：TMR4_3 双组 H 桥 + 缓启动状态机（专用调速线程）
 * @note    架构（命令接缝 + 调速线程，支持 duty_pct 调速）：
 *          - act 线程仲裁决策 → ops(fwd/rev/stop) 只写目标(方向+占空比)+释放信号量；
 *          - pwm 调速线程（TASK_PRIO_PWM，10ms tick）推进 ramp：
 *            U/V 两组占空比各自线性逼近目标（低有效：低电平占比 = 占空比%）；
 *          - 极性仅在 U=V=50%（端压差=0、电流连续）时刻切换：
 *            启动 = STOP 态切 run 极性后 ramp；停止 = ramp 回 50/50 后切 stop 极性；
 *          - 换向/调速不切极性：U/V 目标互换，ramp 自然穿过 50/50（无电流突变）；
 *          - 急停 EStop：跳过 ramp 立即混合极性 50/50（参考 dev_motor 停止行为，
 *            互补 50% = 动态刹车），并撤销未决命令防止线程自启动。
 */
#include "dev_pwm.h"
#include "hc32_drv_pwm.h"
#include "rtt_manager.h"
#include "dev_act.h"
#include <rtthread.h>
#include <rthw.h>   /* rt_hw_interrupt_disable / enable */

#if !PWM_DRV_USE_TMR4
#error "dev_pwm 现行为 TMR4_3 版本；如需切回旧 TMRA 实现请从版本历史恢复本文件"
#endif

/* ===================== 命令接缝（act/任意上下文写 → pwm 线程读；方向值宏见 dev_pwm.h） ===================== */
static volatile uint8_t s_cmd_dir;      /* 目标方向：PWM_CMD_DIR_x（0=停止 1=fwd 2=rev） */
static volatile uint8_t s_cmd_duty;     /* 目标有效占空比%（0=用默认） */
static struct rt_semaphore s_cmd_sem;

/* 写命令（关中断保证 dir/duty 成对可见），随后开中断再释放信号量 */
static void Cmd_Post(uint8_t dir, uint8_t duty_pct)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_cmd_dir = dir;
    s_cmd_duty = duty_pct;
    rt_hw_interrupt_enable(level);
    (void)rt_sem_release(&s_cmd_sem);
}

/* 读命令快照（关中断保证 dir/duty 成对读取） */
static void Cmd_Fetch(uint8_t *dir, uint8_t *duty)
{
    rt_base_t level = rt_hw_interrupt_disable();
    *dir = s_cmd_dir;
    *duty = s_cmd_duty;
    rt_hw_interrupt_enable(level);
}

/* ===================== 调速线程私有状态 ===================== */
typedef enum {
    PWM_ST_STOP = 0,    /* 停止态：U=V=50 + 混合极性（H/L 互补，端压差≈0） */
    PWM_ST_RAMP,        /* 缓启动/缓停/换向中：占空比逐步逼近目标 */
    PWM_ST_RUN,         /* 运行态：已到目标占空比 */
} PwmState_t;

static PwmState_t s_st = PWM_ST_STOP;
static uint8_t s_run_dir;           /* 当前运行方向（RAMP/RUN 态有效） */
static uint8_t s_u_cur = 50U;       /* 当前 U 组占空比%（低有效） */
static uint8_t s_v_cur = 50U;       /* 当前 V 组占空比%（低有效） */
static uint8_t s_u_tgt = 50U;       /* 目标 U 组占空比% */
static uint8_t s_v_tgt = 50U;       /* 目标 V 组占空比% */
static uint8_t s_u_applied = 0xFFU; /* 已写入硬件的占空比（diff 才写 OCCR） */
static uint8_t s_v_applied = 0xFFU;
static rt_tick_t s_last_tick;
static uint8_t s_inited = 0U;

/* 单字节步进：向 tgt 逼近一步（下坡防下溢） */
static uint8_t RampStep8(uint8_t cur, uint8_t tgt, uint8_t step)
{
    if (cur < tgt) {
        cur += step;
        if (cur > tgt) {
            cur = tgt;
        }
    } else if (cur > tgt) {
        cur = (cur > (uint8_t)(tgt + step)) ? (uint8_t)(cur - step) : tgt;
    }
    return cur;
}

/* 占空比写硬件（低有效：低电平占比 = duty%；diff 才写 OCCR，peak 缓冲装载） */
static void ApplyDuty(uint8_t *applied, uint8_t grp, uint8_t duty)
{
    if (*applied != duty) {
        PwmHw4_SetCompare(grp, PwmHw4_DutyToCompare(duty));
        *applied = duty;
    }
}

/* 10ms tick：读目标 → 状态机推进 → 写硬件 */
static void Pwm_RampTick(void)
{
    uint8_t dir, duty, step;

    Cmd_Fetch(&dir, &duty);

    /* 目标换算（对齐参考 dev_motor：fwd V=eff/U=100-eff，rev 互换，停止 U=V=50） */
    if (dir == PWM_CMD_DIR_STOP) {
        s_u_tgt = 50U;
        s_v_tgt = 50U;
    } else {
        uint8_t eff = (duty == 0U) ? (uint8_t)PWM_RUN_DUTY_PCT : duty;
        if (eff < PWM_DUTY_MIN) {
            eff = PWM_DUTY_MIN;
        }
        if (eff > PWM_DUTY_MAX) {
            eff = PWM_DUTY_MAX;
        }
        if (dir == PWM_CMD_DIR_FWD) {
            s_v_tgt = eff;
            s_u_tgt = (uint8_t)(100U - eff);
        } else {
            s_u_tgt = eff;
            s_v_tgt = (uint8_t)(100U - eff);
        }
    }

    if (s_st == PWM_ST_STOP) {
        if (dir == PWM_CMD_DIR_STOP) {
            return;     /* 停止态无命令：硬件保持混合极性 50/50 */
        }
        /* 启动：U=V=50（端压差=0）时刻切 run 极性 → 电流连续 */
        s_run_dir = dir;
        s_u_cur = 50U;
        s_v_cur = 50U;
        s_u_applied = 0xFFU;
        s_v_applied = 0xFFU;
        PwmHw4_SetRunPolarity(PWM_GRP_U);
        PwmHw4_SetRunPolarity(PWM_GRP_V);
        PWM_PRINT("start dir=%u eff=%u (run polarity @50/50)",
                  (unsigned)dir, (unsigned)((duty == 0U) ? PWM_RUN_DUTY_PCT : duty));
        s_st = PWM_ST_RAMP;
    }

    /* ramp 步进：停止用快步长，运行/换向用正常步长 */
    step = (dir == PWM_CMD_DIR_STOP) ? (uint8_t)PWM_RAMP_STEP_STOP_PCT
                                 : (uint8_t)PWM_RAMP_STEP_PCT;
    s_u_cur = RampStep8(s_u_cur, s_u_tgt, step);
    s_v_cur = RampStep8(s_v_cur, s_v_tgt, step);
    ApplyDuty(&s_u_applied, PWM_GRP_U, s_u_cur);
    ApplyDuty(&s_v_applied, PWM_GRP_V, s_v_cur);

    /* 状态跃迁 */
    if ((s_u_cur == s_u_tgt) && (s_v_cur == s_v_tgt)) {
        if (dir == PWM_CMD_DIR_STOP) {
            /* ramp 到 50/50：切混合极性（组内 L 反相 = 互补 50% = 停止态） */
            PwmHw4_SetStopPolarity(PWM_GRP_U);
            PwmHw4_SetStopPolarity(PWM_GRP_V);
            s_st = PWM_ST_STOP;
            PWM_PRINT("stopped (stop polarity @50/50)");
        } else {
            if (s_run_dir != dir) {
                PWM_PRINT("dir -> %s", (dir == PWM_CMD_DIR_FWD) ? "fwd" : "rev");
                s_run_dir = dir;
            }
            s_st = PWM_ST_RUN;
            PWM_PRINT("run u=%u v=%u", (unsigned)s_u_cur, (unsigned)s_v_cur);
        }
    } else {
        s_st = PWM_ST_RAMP;     /* 逼近中（含换向/调速） */
        if (dir != PWM_CMD_DIR_STOP) {
            s_run_dir = dir;
        }
    }
}

/* 调速线程入口：10ms tick（信号量提前唤醒仅刷新目标，ramp 恒速推进） */
static void Pwm_ThreadEntry(void *param)
{
    (void)param;
    while (1) {
        (void)rt_sem_take(&s_cmd_sem, rt_tick_from_millisecond(PWM_TICK_MS));
        Task_Set_Beat();
        /* 命令风暴防护：距上次推进不足一个 tick 则跳过（ramp 恒速，命令下 tick 生效） */
        rt_tick_t now = rt_tick_get();
        if ((rt_tick_t)(now - s_last_tick) < rt_tick_from_millisecond(PWM_TICK_MS)) {
            continue;
        }
        s_last_tick = now;
        Pwm_RampTick();
    }
}

/* ===================== 仲裁输出 ops（act 线程上下文调用） ===================== */
static void Pwm_OpFwd(uint8_t axis_id, uint8_t duty_pct)
{
    (void)axis_id;
    Cmd_Post(PWM_CMD_DIR_FWD, duty_pct);
}

static void Pwm_OpRev(uint8_t axis_id, uint8_t duty_pct)
{
    (void)axis_id;
    Cmd_Post(PWM_CMD_DIR_REV, duty_pct);
}

static void Pwm_OpStop(uint8_t axis_id)
{
    (void)axis_id;
    Cmd_Post(PWM_CMD_DIR_STOP, 0U);
}

static const ArbOutputOps_t s_pwm_out_ops = {
    Pwm_OpFwd,
    Pwm_OpRev,
    Pwm_OpStop,
};

/* ===================== 公开 API ===================== */
int Dev_PwmMotor_RunFwd(void)
{
    if (s_inited == 0U) {
        return -1;
    }
    Cmd_Post(PWM_CMD_DIR_FWD, 0U);
    return 0;
}

int Dev_PwmMotor_RunRev(void)
{
    if (s_inited == 0U) {
        return -1;
    }
    Cmd_Post(PWM_CMD_DIR_REV, 0U);
    return 0;
}

int Dev_PwmMotor_Stop(void)
{
    if (s_inited == 0U) {
        return -1;
    }
    Cmd_Post(PWM_CMD_DIR_STOP, 0U);
    return 0;
}

int Dev_PwmMotor_EStop(void)
{
    /* 急停：撤销未决命令 + 立即混合极性 50/50（参考 dev_motor 停止 = 动态刹车）。
       关中断保证寄存器序列原子；PwmHw4_* 为纯寄存器写，ISR 上下文可调。 */
    rt_base_t level = rt_hw_interrupt_disable();
    s_cmd_dir = PWM_CMD_DIR_STOP;
    s_cmd_duty = 0U;
    PwmHw4_SetCompare(PWM_GRP_U, PwmHw4_DutyToCompare(50U));
    PwmHw4_SetCompare(PWM_GRP_V, PwmHw4_DutyToCompare(50U));
    PwmHw4_SetStopPolarity(PWM_GRP_U);
    PwmHw4_SetStopPolarity(PWM_GRP_V);
    s_u_cur = 50U;
    s_v_cur = 50U;
    s_u_applied = 50U;
    s_v_applied = 50U;
    s_st = PWM_ST_STOP;
    rt_hw_interrupt_enable(level);
    return 0;
}

/* ===================== 初始化 ===================== */
void Dev_Pwm_Init(void)
{
    if (s_inited != 0U) {
        return;     /* IDLE 重入：跳过（硬件配置不变，无需重复初始化） */
    }

    /* 硬件：TMR4_3 + 4 引脚复用（绑定见 hc32_drv_pwm.h）+ 上电安全态 = 停止态（混合极性 50/50） */
    PwmHw4_Init(PWM_FREQ_DFT_HZ);

    if (rt_sem_init(&s_cmd_sem, "pwmsem", 0, RT_IPC_FLAG_FIFO) != RT_EOK) {
        PWM_PRINT("sem init failed!");
        return;
    }

    /* 调速线程：10ms tick（饿死检测登记 beat=10ms） */
    if (Task_Set_Create("pwm", Pwm_ThreadEntry, RT_NULL,
                        TASK_STACK_PWM, TASK_PRIO_PWM, PWM_TICK_MS) == RT_NULL) {
        PWM_PRINT("thread create failed!");
        return;     /* 不绑定 ops：arb 调用空 ops 会失败打印，状态可观察 */
    }

    /* 绑定仲裁输出：此后 arb 决策经 ops 投递目标到调速线程 */
    (void)Arb_BindOutputOps(&s_pwm_out_ops);

    s_inited = 1U;
    PWM_PRINT("init tmr4_3 stop=50/50 mixed");
}

void Dev_Pwm_Task(void)
{
    /* ramp 已内化到 pwm 调速线程（10ms tick），本函数保留兼容注册表签名 */
}

/* EOF */
