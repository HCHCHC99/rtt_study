/**
 * @file    dev_sm_thread.c
 * @brief   系统状态机线程（事件驱动）
 * @note    替代原 Sys_State_Task 轮询：rt_event_recv 阻塞 -> Sys_State_Dispatch
 */
#include "dev_model.h"
#include "dev_state.h"
#include "dev_event_def.h"
#include "rtt_manager.h"
#include <rtthread.h>

/* 线程等待的系统事件位（与 sys_jump 表使用的事件一致） */

/* INIT 卡死诊断计数：main.c 每秒随 [SM_DIAG] 打印（打印可能被 RTT 丢，变量不会） */
volatile uint32_t g_sm_diag_entered = 0U;
volatile uint32_t g_sm_diag_recv_ok = 0U;

static void sys_sm_thread_entry(void *param)
{
    rt_uint32_t e;
    char evt_name[64];

    (void)param;
    g_sm_diag_entered = 1U;
    MAIN_D_SYNC("[SYS_STATE] sm thread entry");
    while (1) {
        if (rt_event_recv(mySystem.sys_evt, SYS_SM_WAIT_EVENTS,
                          RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                          RT_WAITING_FOREVER, &e) == RT_EOK) {
            g_sm_diag_recv_ok++;
            Sys_EventBitsName(e, evt_name, (rt_uint32_t)sizeof(evt_name));
            MAIN_D_SYNC("[SYS_STATE] sm dispatch evt=0x%08x (%s)", (unsigned)e, evt_name);
            Sys_State_Dispatch(e);
        }
        else {
            SYS_STATE_PRINT("sm recv err");
        }
    }
}

void Sys_Sm_Thread_Start(void)
{
    rt_thread_t th = rt_thread_create("sys_sm",
                                      sys_sm_thread_entry,
                                      RT_NULL,
                                      SYS_SM_THREAD_STACK,
                                      SYS_SM_THREAD_PRIO,
                                      SYS_SM_THREAD_TICK);
    if (th != RT_NULL) {
        rt_err_t ret = rt_thread_startup(th);
        MAIN_D_SYNC("[SYS_STATE] sm thread started prio=%d ret=%d",
                    (int)SYS_SM_THREAD_PRIO, (int)ret);
    } else {
        SYS_STATE_PRINT("thread create failed");
    }
}

/* EOF */







