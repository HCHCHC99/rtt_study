/**
 * @file    dev_event_def.h
 * @brief   系统事件位定义（表驱动状态机移植）
 * @note    原样裁剪自控制器工程 dev_event_def.h，只保留系统状态机当前用到的事件位；
 *          轴事件为多轴预留（系统状态机 enter 回调向轴下发使能/保持）。
 */
#ifndef __DEV_EVENT_DEF_H__
#define __DEV_EVENT_DEF_H__

#include <stdint.h>

/* ===================== 系统事件组 ===================== */
#define EVT_SYS_INIT_DONE              (1U << 0)    /* 系统初始化完成 */
#define EVT_SYS_FAULT                  (1U << 1)    /* 系统故障 */
#define EVT_SYS_EMERGENCY              (1U << 2)    /* 系统急停 */
#define EVT_SYS_RECOVERY               (1U << 3)    /* 故障恢复 */
#define EVT_SYS_VOLT_NORMAL            (1U << 4)    /* 电压正常 */
#define EVT_SYS_VOLT_OVER              (1U << 5)    /* 过压 */
#define EVT_SYS_VOLT_UNDER             (1U << 6)    /* 欠压 */
#define EVT_SYS_CMD_WORK_ENABLE        (1U << 7)    /* 系统工作：使能 */
#define EVT_SYS_OVER_CURRENT          (1U << 8)    /* 过流 */
#define EVT_SYS_ROD_LIMIT_FAULT        (1U << 9)    /* 推杆上下霍尔故障（双高异常） */
#define EVT_SYS_VOLT_RECOVER_WAIT      (1U << 10)   /* 电压回到正常区，开始等恢复延时（dev_bus_voltage ISR 发） */
#define EVT_SYS_POLARITY_CHG           (1U << 11)   /* 电源极性跳变沿（dev_polarity 发；决策在 dev_state：方向相关清过流故障） */
#define EVT_SYS_ST_WORK_ERROR          (1U << 13)   /* 系统工作：工作错误 */

/* ===================== 轴事件组（多轴预留） ===================== */
#define EVT_ACT_WORK_ENABLE            (1U << 0)    /* 推杆工作使能 */
#define EVT_ACT_WORK_DISABLE           (1U << 1)    /* 推杆工作禁止 */
#define EVT_ACT_POLARITY_FWD           (1U << 2)    /* 电源极性：正向（P=1,N=0） */
#define EVT_ACT_POLARITY_REV           (1U << 3)    /* 电源极性：反向（P=0,N=1） */
#define EVT_ACT_POWER_ABNORMAL         (1U << 4)    /* 电源异常（P=1,N=1） */
#define EVT_ACT_POWER_LOST             (1U << 5)    /* 掉电（P=0,N=0） */
#define EVT_ROD_LIMIT_EXTEND           (1U << 6)    /* 推杆到达上限位（通知仲裁） */
#define EVT_ROD_LIMIT_RETRACT          (1U << 7)    /* 推杆到达下限位（通知仲裁） */
#define EVT_ROD_LIMIT_RELEASED         (1U << 8)    /* 推杆离开限位（通知仲裁） */
#define EVT_ACT_HOLD                   (1U << 14)   /* 推杆保持/制动 */

#endif /* __DEV_EVENT_DEF_H__ */

/* 系统状态机线程等待的事件位（与 sys_jump 表一致） */
#define SYS_SM_WAIT_EVENTS \
    (EVT_SYS_INIT_DONE | EVT_SYS_CMD_WORK_ENABLE | EVT_SYS_FAULT | EVT_SYS_EMERGENCY | \
     EVT_SYS_RECOVERY | EVT_SYS_VOLT_OVER | EVT_SYS_VOLT_UNDER | EVT_SYS_VOLT_NORMAL | \
     EVT_SYS_OVER_CURRENT | EVT_SYS_ROD_LIMIT_FAULT | EVT_SYS_ST_WORK_ERROR | \
     EVT_SYS_VOLT_RECOVER_WAIT | EVT_SYS_POLARITY_CHG)


