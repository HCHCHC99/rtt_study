#ifndef __DEV_REGISTRY_H__
#define __DEV_REGISTRY_H__

#include <stdint.h>
#include "task_set.h"     /* 栈大小/优先级统一管理 */

/* 配置宏（统一放头文件） */
#define MAX_REG_MODULES         (16U)
#define DEV_THREAD_STACK_SIZE   TASK_STACK_DEV
#define DEV_THREAD_PRIORITY     TASK_PRIO_DEV
#define DEV_THREAD_TICK         (10)

typedef struct {
    const char *name;
    void (*init)(void);

    /* B mode: cooperative periodic task driven by Dev_Registry_UpdateAll. */
    void (*task)(void);
    uint16_t    period_ms;

    /* C mode: registry creates and starts one thread for this entry. */
    void    (*thread_entry)(void *param);
    uint16_t thread_stack;   /* stack size in bytes */

    uint8_t  prio;           /* lower value means higher priority */
    uint32_t    last_tick;
    uint8_t     enabled;
} SysModule_t;

#define SYS_MODULE_REGISTER(_name, _init, _task, _prio, _period) \
    { #_name, _init, _task, _period, RT_NULL, 0, _prio, 0, 1 }

#define SYS_MODULE_REGISTER_THREAD(_name, _init, _thread, _prio, _stack) \
    { #_name, _init, RT_NULL, 0, _thread, _stack, _prio, 0, 1 }

int  Dev_Registry_Add(const SysModule_t *module);
void Dev_RegisterAll(void);   /* 集中注册：adc / cur / vm */
void Dev_Registry_InitAll(void);
void Dev_Registry_UpdateAll(void);
void Dev_Thread_Entry(void *param);
int  Dev_Start(void);

#endif /* __DEV_REGISTRY_H__ */
