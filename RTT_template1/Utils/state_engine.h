/**
 * @file    state_engine.h
 * @brief   通用事件驱动状态机引擎（支持多实例、事件跳转、Enter/Exit 回调）
 * @note    纯引擎层，与业务完全解耦，可对接任意事件组系统
 */

#ifndef __STATE_ENGINE_H
#define __STATE_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 基础类型定义 ============================== */

/**
 * @brief 事件类型（与事件组引擎兼容，uint32 事件位）
 */
typedef uint32_t Event_t;

/**
 * @brief 状态类型
 */
typedef uint32_t State_t;

/**
 * @brief 状态回调函数（无参数，你要求的标准格式）
 */
typedef void (*StateFunc_t)(void);
// typedef void (*StateFunc_t)(void *arg);

/* ============================== 状态机核心结构 ============================== */

/**
 * @brief 状态跳转表项
 * @note  当前状态 + 触发事件 = 目标状态
 */
typedef struct {
    State_t cur_state;      /* 当前状态 */
    Event_t event;          /* 触发事件 */
    State_t target_state;   /* 跳转目标状态 */
} StateJumpTable_t;

/**
 * @brief 状态函数表项（Enter / Exit 回调）
 */
typedef struct {
    State_t     state;      /* 状态 */
    StateFunc_t enter;      /* 进入状态执行 */
    StateFunc_t exit;       /* 退出状态执行 */
} StateFuncTable_t;

/**
 * @brief 通用状态机对象（支持多实例）
 */
typedef struct {
    const StateJumpTable_t *jump_table;    /* 状态跳转表 */
    uint16_t jump_table_size;              /* 跳转表项数 */

    const StateFuncTable_t *func_table;    /* 状态函数表 */
    uint16_t func_table_size;              /* 函数表项数 */

    State_t cur_state;                     /* 当前状态 */
    State_t previous_state;                /* 上一状态 */
    State_t init_state;                    /* 初始状态 */
} StateMachine_t;

/* ============================== 引擎 API ============================== */

/**
 * @brief  初始化状态机
 * @param  sm: 状态机实例指针
 * @return 无
 */
void StateMachine_Init(StateMachine_t *sm);

/**
 * @brief  向状态机发送一个事件（核心驱动接口）
 * @param  sm: 状态机实例
 * @param  event: 触发事件
 * @return 无
 */
void StateMachine_SendEvent(StateMachine_t *sm, Event_t event);

/**
 * @brief  获取状态机当前状态
 * @param  sm: 状态机
 * @return 当前状态
 */
State_t StateMachine_GetState(StateMachine_t *sm);

#ifdef __cplusplus
}
#endif

#endif /* __STATE_ENGINE_H */
