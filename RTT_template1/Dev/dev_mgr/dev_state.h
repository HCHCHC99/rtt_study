/**
 * @file    dev_state.h
 * @brief   系统状态机接口（表驱动移植）
 */
#ifndef __DEV_STATE_H__
#define __DEV_STATE_H__

#include "dev_model.h"

/* 系统状态枚举（原样移植自控制器工程 dev_state.h） */
typedef enum {
    SYS_STATE_INIT = 0,
    SYS_STATE_IDLE,
    SYS_STATE_RUN,
    SYS_STATE_FAULT,
    SYS_STATE_EMERGENCY,
    SYS_STATE_STOP,
} SysState_t;

/* 系统故障码（写入 System_t.error_code） */
#define SYS_ERR_NONE             0U
#define SYS_ERR_OVER_CURRENT     1U
#define SYS_ERR_VOLT_OVER        2U
#define SYS_ERR_VOLT_UNDER       3U
#define SYS_ERR_ROD_LIMIT        4U   /* 推杆上下霍尔故障 */

/* 系统状态机初始化：填表 + 初始化（原样移植） */
void Sys_State_Init(StateMachine_t *sm);

/* 事件位分发：把收到的系统事件位逐个投递给状态机（替代原 Sys_State_Task 轮询） */
void Sys_State_Dispatch(rt_uint32_t bits);

/* 向系统事件集发送事件（线程/ISR 通用入口，rt_event_send 中断安全） */
void Sys_Event_Send(rt_uint32_t bits);

/* 手动恢复：清故障码 + 发 EVT_SYS_RECOVERY（触发方式后续接入 MSH/按键） */
void Sys_State_Recover(void);

/* 事件位图 → 名字串（诊断打印；如 "INIT_DONE|WORK_ENABLE"，空集为 "NONE"） */
void Sys_EventBitsName(rt_uint32_t bits, char *buf, rt_uint32_t size);

#endif /* __DEV_STATE_H__ */




