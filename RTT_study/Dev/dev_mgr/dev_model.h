/**
 * @file    dev_model.h
 * @brief   顶层系统对象模型（表驱动状态机移植：对象构建 + 多轴预留）
 * @note    对应控制器工程 System_t / Axis_t 的最小裁剪版；
 *          Route B 扩展时再向 Axis_t 追加电机、PID、act/mot 状态机等字段。
 */
#ifndef __DEV_MODEL_H__
#define __DEV_MODEL_H__

#include "state_engine.h"
#include "task_set.h"     /* 栈大小/优先级统一管理 */
#include "dev_rod_position.h"
#include "dev_rod_state.h"

/* 系统状态机线程配置（统一放头文件） */
#define SYS_SM_THREAD_STACK   TASK_STACK_SYS_SM
#define SYS_SM_THREAD_PRIO    TASK_PRIO_SYS_SM
#define SYS_SM_THREAD_TICK    10
#include <rtthread.h>

/* 最大轴数（多轴预留：与控制器工程 MAX_AXIS_NUM 一致） */
#define MAX_AXIS_NUM                    2

/* 轴方向（预留：系统状态机 enter 回调据此判断该轴是否有效/已配置） */
typedef enum {
    ACT_DIR_NONE = 0,                  /* 未配置/无效 */
    ACT_DIR_COMBINE_IS_MOVE_OUT,       /* 结合方向为伸出 */
    ACT_DIR_COMBINE_IS_MOVE_IN,        /* 结合方向为缩回 */
    ACT_DIR_MAX
} AxisDir_t;

/* 轴对象（预留骨架：Route B 再移植 act/mot 状态机表与设备层） */
typedef struct {
    uint8_t        id;                 /* 轴序号 0..MAX_AXIS_NUM-1 */
    AxisDir_t      dir;                /* ACT_DIR_NONE 表示未配置 */
    StateMachine_t sm_act;             /* 推杆状态机（预留，暂不挂表） */
    rt_event_t     evt_act;            /* 推杆事件组（预留：系统层向轴下发事件） */
    RodPosition_t  position;             /* 推杆位置模块 */
    RodStateCtx_t  state;                /* 推杆状态模块上下文 */
} Axis_t;

/* 顶层系统对象（对应控制器工程 System_t 的最小裁剪版，已预留多轴） */
typedef struct {
    StateMachine_t sys_sm;             /* 系统状态机 */
    rt_event_t     sys_evt;            /* 系统事件集 */
    uint32_t       error_code;         /* 系统故障码：0=无 1=过流 2=过压 3=欠压 */
    uint32_t       fault_bits;         /* 故障位图：bit0 过流 bit1 过压 bit2 欠压；全清才自动恢复 */
    State_t        prev_state;        /* 故障前状态（恢复时回跳：RUN->RUN，否则->IDLE） */
    Axis_t         axis[MAX_AXIS_NUM]; /* 多轴预留 */
} System_t;

extern System_t mySystem;

/* 对象构建：系统状态机填表 + 事件集创建 + 轴对象初始化 + 上电自动启动 */
void App_Model_Init(void);

/* 启动系统状态机线程（rt_event_recv 阻塞 -> StateMachine_SendEvent） */
void Sys_Sm_Thread_Start(void);

/* 向所有轴事件组广播事件（ISR 安全；电机控制/电源极性等用） */
void Act_Event_Send(rt_uint32_t bits);

#endif /* __DEV_MODEL_H__ */







