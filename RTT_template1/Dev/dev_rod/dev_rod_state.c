/**
 * @file    dev_rod_state.c
 * @brief   推杆状态模块实现：表驱动状态机 + 事件合成 + 限位事件发送
 * @note    StateMachine 实例用 Axis_t.sm_act；事件由 RodState_Update 每 10ms 合成一次。
 */
#include "dev_rod_state.h"
#include "dev_model.h"       /* mySystem / Act_Event_Send */
#include "dev_event_def.h"
#include "dev_act.h"         /* Arb_SendCommand: 限位清允许命令 */
#include "rtt_manager.h"
#include <rtthread.h>

/* ============ 跳转表 ============ */
static const StateJumpTable_t s_rod_jump[] = {
    /* 未知（未校准）：允许运动以寻找限位，碰到限位即校准 */
    {ROD_STATE_UNKNOWN,     ROD_EVT_CMD_STOP,      ROD_STATE_STOPPED},
    {ROD_STATE_UNKNOWN,     ROD_EVT_CMD_EXTEND,    ROD_STATE_EXTENDING},
    {ROD_STATE_UNKNOWN,     ROD_EVT_CMD_RETRACT,   ROD_STATE_RETRACTING},
    {ROD_STATE_UNKNOWN,     ROD_EVT_AT_MAX,        ROD_STATE_EXT_LIMIT},
    {ROD_STATE_UNKNOWN,     ROD_EVT_AT_MIN,        ROD_STATE_RET_LIMIT},

    {ROD_STATE_STOPPED,     ROD_EVT_CMD_EXTEND,    ROD_STATE_EXTENDING},
    {ROD_STATE_STOPPED,     ROD_EVT_CMD_RETRACT,   ROD_STATE_RETRACTING},
    {ROD_STATE_STOPPED,     ROD_EVT_AT_MAX,        ROD_STATE_EXT_LIMIT},
    {ROD_STATE_STOPPED,     ROD_EVT_AT_MIN,        ROD_STATE_RET_LIMIT},

    {ROD_STATE_EXTENDING,   ROD_EVT_CMD_STOP,      ROD_STATE_STOPPED},
    {ROD_STATE_EXTENDING,   ROD_EVT_CMD_RETRACT,   ROD_STATE_RETRACTING},
    {ROD_STATE_EXTENDING,   ROD_EVT_AT_MAX,        ROD_STATE_EXT_LIMIT},
    {ROD_STATE_EXTENDING,   ROD_EVT_TIMEOUT,       ROD_STATE_EXT_FAULT},
    {ROD_STATE_EXTENDING,   ROD_EVT_SENSOR_FAULT,  ROD_STATE_EXT_FAULT},

    {ROD_STATE_RETRACTING,  ROD_EVT_CMD_STOP,      ROD_STATE_STOPPED},
    {ROD_STATE_RETRACTING,  ROD_EVT_CMD_EXTEND,    ROD_STATE_EXTENDING},
    {ROD_STATE_RETRACTING,  ROD_EVT_AT_MIN,        ROD_STATE_RET_LIMIT},
    {ROD_STATE_RETRACTING,  ROD_EVT_TIMEOUT,       ROD_STATE_RET_FAULT},
    {ROD_STATE_RETRACTING,  ROD_EVT_SENSOR_FAULT,  ROD_STATE_RET_FAULT},

    /* 限位态忽略 CMD_STOP（保持限位直到反向命令）：软限位无持续稳态源，
       若退回 STOPPED 会导致 limit_ext/ret_sent 不复位、下次到限位不发事件 */
    {ROD_STATE_EXT_LIMIT,   ROD_EVT_CMD_RETRACT,   ROD_STATE_RETRACTING},

    {ROD_STATE_RET_LIMIT,   ROD_EVT_CMD_EXTEND,    ROD_STATE_EXTENDING},

    {ROD_STATE_EXT_FAULT,   ROD_EVT_CMD_STOP,      ROD_STATE_STOPPED},
    {ROD_STATE_EXT_FAULT,   ROD_EVT_CMD_EXTEND,    ROD_STATE_EXTENDING},
    {ROD_STATE_EXT_FAULT,   ROD_EVT_CMD_RETRACT,   ROD_STATE_RETRACTING},

    {ROD_STATE_RET_FAULT,   ROD_EVT_CMD_STOP,      ROD_STATE_STOPPED},
    {ROD_STATE_RET_FAULT,   ROD_EVT_CMD_EXTEND,    ROD_STATE_EXTENDING},
    {ROD_STATE_RET_FAULT,   ROD_EVT_CMD_RETRACT,   ROD_STATE_RETRACTING},
};

/* 状态名（日志用，英文） */
static const char *const s_rod_state_name[] = {
    "UNKNOWN", "STOPPED", "EXTENDING", "EXT_LIMIT", "EXT_FAULT",
    "RETRACTING", "RET_LIMIT", "RET_FAULT",
};

void RodState_Init(StateMachine_t *sm, RodStateCtx_t *ctx, uint8_t axis_id, const RodPosition_t *pos)
{
    sm->jump_table      = s_rod_jump;
    sm->jump_table_size = (uint16_t)(sizeof(s_rod_jump) / sizeof(StateJumpTable_t));
    sm->func_table      = RT_NULL;   /* 侧效应在 RodState_Update 处理 */
    sm->func_table_size = 0U;
    sm->init_state      = ROD_STATE_UNKNOWN;
    StateMachine_Init(sm);

    ctx->axis_id           = axis_id;
    ctx->position          = pos;
    ctx->direction         = ROD_DIR_STOP;
    ctx->calib_req         = ROD_CALIB_REQ_NONE;
    ctx->fault_code        = 0U;
    ctx->move_start_tick   = 0U;
    ctx->move_timeout_ms   = 5000U;
    ctx->limit_ext_sent    = false;
    ctx->limit_ret_sent    = false;
    ctx->extend_count      = 0U;
    ctx->retract_count     = 0U;
    ctx->fault_count       = 0U;
    ctx->limit_reach_count = 0U;
}

/* 事件合成：优先级 限位 > 超时 > 方向指令
   限位源：软限位校准请求（sys_sm 过流判定写入，rod_task 消费） */
static RodEvent_t RodState_SynthesizeEvent(const RodStateCtx_t *ctx, RodState_t st,
                                           RodDirection_t dir, uint32_t tick)
{
    if (ctx->calib_req == ROD_CALIB_REQ_MAX) {
        return ROD_EVT_AT_MAX;
    }
    if (ctx->calib_req == ROD_CALIB_REQ_MIN) {
        return ROD_EVT_AT_MIN;
    }
    /* 位置停车（与板无关）：位置已校准且到达"行程-停止裕量"即合成 AT_MAX，不再依赖过流。
       仅停机决策：绝不写 calib_state/calib_pending/position_mm（校准只由过流触发，不做钳位）。
       状态门与过流校准路径的 AT_MAX 一致（跳转表 UNKNOWN/STOPPED/EXTENDING 均有对应行，
       EXT_LIMIT 本身无行即驻留）；calib_req（过流）路径优先级保持在前。 */
    if ((ctx->position != NULL) &&
        (ctx->position->calib_state == POSITION_CALIBRATED) &&
        (ctx->position->position_mm >=
         (ctx->position->stroke_mm - ctx->position->stop_margin_mm)) &&
        (st == ROD_STATE_UNKNOWN || st == ROD_STATE_STOPPED ||
         st == ROD_STATE_EXTENDING || st == ROD_STATE_EXT_LIMIT)) {
        return ROD_EVT_AT_MAX;
    }
    if ((st == ROD_STATE_EXTENDING || st == ROD_STATE_RETRACTING) &&
        ctx->move_timeout_ms != 0U && (tick - ctx->move_start_tick) >= ctx->move_timeout_ms) {
        return ROD_EVT_TIMEOUT;
    }
    if (dir == ROD_DIR_FWD)  return ROD_EVT_CMD_EXTEND;
    if (dir == ROD_DIR_REV)  return ROD_EVT_CMD_RETRACT;
    return ROD_EVT_CMD_STOP;
}

void RodState_Update(StateMachine_t *sm, RodStateCtx_t *ctx, RodDirection_t dir, uint32_t tick)
{
    RodState_t prev, cur;
    RodEvent_t evt;

    ctx->direction = dir;
    prev = (RodState_t)StateMachine_GetState(sm);

    evt = RodState_SynthesizeEvent(ctx, prev, dir, tick);
    StateMachine_SendEvent(sm, (Event_t)evt);
    cur = (RodState_t)StateMachine_GetState(sm);
    ctx->calib_req = ROD_CALIB_REQ_NONE;   /* 校准请求一次性消费 */

    /* ---- 状态侧效应（按跳变处理） ---- */
    if (cur == ROD_STATE_EXTENDING) {
        if (prev != ROD_STATE_EXTENDING) {
            ctx->extend_count++;
            ctx->move_start_tick = tick;
        }
        if (prev == ROD_STATE_RET_LIMIT) {
            /* 离开下限位（跳转表 RET_LIMIT 仅经 CMD_EXTEND 去 EXTENDING，原代码误置于
               EXT_LIMIT 检测，RELEASED 从不触发——本次修正）：
               复位 sent + 发 RELEASED + 解除 BLOCK 压制 */
            ctx->limit_ret_sent = false;
            Act_Event_Send(EVT_ROD_LIMIT_RELEASED);
            /* UNBLOCK_REV：BLOCK 是持久状态，不解除则 REV 命令被 SetAllow 永久拒收
               （dev_act.c block 队列非空即 return）。UNBLOCK 后 allow 已空
               （进限位沿已 CLEAR），无旧命令复活 */
            (void)Arb_SendCommand(ctx->axis_id, DEV_ID_ROD_LIMIT_REV, PRIO_LIMIT,
                                  CMD_TYPE_UNBLOCK_REV, 0U, RT_TRUE);
        }
    } else if (cur == ROD_STATE_RETRACTING) {
        if (prev != ROD_STATE_RETRACTING) {
            ctx->retract_count++;
            ctx->move_start_tick = tick;
        }
        if (prev == ROD_STATE_EXT_LIMIT) {
            /* 离开上限位（跳转表 EXT_LIMIT 仅经 CMD_RETRACT 去 RETRACTING，原代码误置于
               RET_LIMIT 检测——本次修正，语义同上对称）：
               复位 sent + 发 RELEASED + 解除 BLOCK 压制 */
            ctx->limit_ext_sent = false;
            Act_Event_Send(EVT_ROD_LIMIT_RELEASED);
            (void)Arb_SendCommand(ctx->axis_id, DEV_ID_ROD_LIMIT_FWD, PRIO_LIMIT,
                                  CMD_TYPE_UNBLOCK_FWD, 0U, RT_TRUE);
        }
    } else if (cur == ROD_STATE_EXT_LIMIT) {
        if (prev != ROD_STATE_EXT_LIMIT) {
            ctx->limit_reach_count++;
            if (!ctx->limit_ext_sent) {
                Act_Event_Send(EVT_ROD_LIMIT_EXTEND); /* 到上限位 -> 通知仲裁 */
                ctx->limit_ext_sent = true;
            }
            /* 清旧 allow（沿一次）：进限位前已在列表的记录（如 EXTENDING 中的输出命令）
               不会被 BLOCK 清除，UNBLOCK 后会复活——沿上清一次封死复活路径 */
            (void)Arb_SendCommand(ctx->axis_id, DEV_ID_ROD_LIMIT_FWD, PRIO_LIMIT,
                                  CMD_TYPE_CLEAR_ALLOW_FWD, 0U, RT_TRUE);
        }
        /* 限位驻留期每拍重发 BLOCK（状态性压制，替代原 CLEAR 对抗，消除 CMD 后到复活脉冲）：
           BLOCK 期间 Arb_CmdListSetAllow 直接拒收入列（block 队列非空即 return），
           Arb_Decision 输出判定同样被挡（block 非空 -> fwd_cmd=NULL），
           限位期 CMD_FWD 产生零输出（覆盖过流软限位、位置停车、上电恢复超程三路径）。
           SetBlock 同 device_id 幂等；冗余重发为有意安全设计（at-least-once，
           BLOCK 消息丢失下一拍兜底），勿优化为"只发一次" */
        (void)Arb_SendCommand(ctx->axis_id, DEV_ID_ROD_LIMIT_FWD, PRIO_LIMIT,
                              CMD_TYPE_BLOCK_FWD, 0U, RT_TRUE);
    } else if (cur == ROD_STATE_RET_LIMIT) {
        if (prev != ROD_STATE_RET_LIMIT) {
            ctx->limit_reach_count++;
            if (!ctx->limit_ret_sent) {
                Act_Event_Send(EVT_ROD_LIMIT_RETRACT); /* 到下限位 -> 通知仲裁 */
                ctx->limit_ret_sent = true;
            }
            /* 清旧 allow（沿一次，语义同上限位分支） */
            (void)Arb_SendCommand(ctx->axis_id, DEV_ID_ROD_LIMIT_REV, PRIO_LIMIT,
                                  CMD_TYPE_CLEAR_ALLOW_REV, 0U, RT_TRUE);
        }
        /* 限位驻留期每拍重发 BLOCK（语义同上限位分支，对称） */
        (void)Arb_SendCommand(ctx->axis_id, DEV_ID_ROD_LIMIT_REV, PRIO_LIMIT,
                              CMD_TYPE_BLOCK_REV, 0U, RT_TRUE);
    } else if (cur == ROD_STATE_EXT_FAULT || cur == ROD_STATE_RET_FAULT) {
        if (prev != cur) {
            ctx->fault_count++;
        }
    }

    /* 状态跳变日志 */
    if (cur != prev) {
        ROD_PRINT("rod%u state=%s", (unsigned)ctx->axis_id, s_rod_state_name[cur]);
    }
}

RodState_t RodState_Get(StateMachine_t *sm)
{
    return (RodState_t)StateMachine_GetState(sm);
}

/* EOF */


