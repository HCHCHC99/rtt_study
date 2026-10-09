/**
 * @file    dev_rod_state.h
 * @brief   推杆状态模块：表驱动状态机（挂 Axis_t.sm_act），判定停止/伸出/限位/故障等
 * @note    事件由 10ms tick 合成（方向/限位/超时/传感器异常），喂给 state_engine；
 *          限位事件经 Act_Event_Send 发仲裁系统。
 */
#ifndef __DEV_ROD_STATE_H__
#define __DEV_ROD_STATE_H__

#include <stdint.h>
#include <stdbool.h>
#include "state_engine.h"
#include "dev_rod_position.h"

/* 推杆运动方向 */
typedef enum {
    ROD_DIR_STOP = 0,
    ROD_DIR_FWD  = 1,    /* 正向（伸出） */
    ROD_DIR_REV  = -1,   /* 反向（缩回） */
} RodDirection_t;

/* 推杆状态 */
typedef enum {
    ROD_STATE_UNKNOWN = 0,    /* 未知（未校准/未初始化） */
    ROD_STATE_STOPPED,        /* 停止 */
    ROD_STATE_EXTENDING,      /* 伸出中 */
    ROD_STATE_EXT_LIMIT,      /* 伸出到上限位 */
    ROD_STATE_EXT_FAULT,      /* 伸出中故障 */
    ROD_STATE_RETRACTING,     /* 缩回中 */
    ROD_STATE_RET_LIMIT,      /* 缩回到下限位 */
    ROD_STATE_RET_FAULT,      /* 缩回中故障 */
} RodState_t;

/* 推杆事件（10ms tick 合成，喂给状态机） */
typedef enum {
    ROD_EVT_NONE = 0,
    ROD_EVT_CMD_STOP,
    ROD_EVT_CMD_EXTEND,
    ROD_EVT_CMD_RETRACT,
    ROD_EVT_AT_MAX,          /* 到达上限位 */
    ROD_EVT_AT_MIN,          /* 到达下限位 */
    ROD_EVT_TIMEOUT,         /* 运动超时 */
    ROD_EVT_SENSOR_FAULT,    /* 传感器异常（预留事件源：现版本无产生方，跳转行保留） */
} RodEvent_t;

/* 软限位校准请求（sys_sm 过流判定写入，rod_task 消费；过流软限位判定链） */
typedef enum {
    ROD_CALIB_REQ_NONE = 0,
    ROD_CALIB_REQ_MAX,       /* 过流+伸出：请求上限位校准（重置为行程） */
    ROD_CALIB_REQ_MIN,       /* 过流+缩回：请求下限位校准（重置为 0） */
} RodCalibReq_t;

/* 推杆状态模块上下文（状态机实例用 Axis_t.sm_act） */
typedef struct {
    uint8_t              axis_id;         /* 轴序号（日志用） */
    const RodPosition_t *position;      /* 只读位置引用 */
    RodDirection_t       direction;     /* 当前方向指令（来自仲裁） */
    volatile uint8_t     calib_req;       /* 软限位校准请求（RodCalibReq_t；sys_sm 写，rod_task 消费清零） */
    uint32_t             fault_code;
    uint32_t             move_start_tick;
    uint32_t             move_timeout_ms;     /* 运动超时 ms（5000，0=禁用） */
    bool                 limit_ext_sent;      /* 防重复发送 */
    bool                 limit_ret_sent;
    uint32_t             extend_count;
    uint32_t             retract_count;
    uint32_t             fault_count;
    uint32_t             limit_reach_count;
} RodStateCtx_t;

void RodState_Init(StateMachine_t *sm, RodStateCtx_t *ctx, uint8_t axis_id, const RodPosition_t *pos);
void RodState_Update(StateMachine_t *sm, RodStateCtx_t *ctx, RodDirection_t dir, uint32_t tick);
RodState_t RodState_Get(StateMachine_t *sm);

#endif /* __DEV_ROD_STATE_H__ */

