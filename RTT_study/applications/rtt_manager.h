/**
 * @file    rtt_manager.h
 * @brief   RTT 打印统一开关管理（各模块打印宏）
 * @note    在"开关区"把对应 _EN 置 0 即可关闭该模块打印，无需改业务代码；
 *          所有打印宏最终落到 rtt_log.h 的 MAIN_D（英文、不打印浮点）。
 */
#ifndef __RTT_MANAGER_H__
#define __RTT_MANAGER_H__

#include "RTT/rtt_log.h"

/* ===================== 开关区（1=启用 0=关闭） ===================== */
#define SYS_STATE_PRINT_EN      0   /* 系统状态机：跳转/入口 */
#define DEV_REG_PRINT_EN        0   /* 设备注册 */
#define POWER_PRINT_EN          1   /* 电源设备：过压/欠压/过流 */
#define POLARITY_PRINT_EN       0   /* 电源极性：跳变/初始化 */
#define ARB_PRINT_EN            0   /* 电机仲裁：决策/队列/复位 */
#define SM_DIAG_PRINT_EN        0   /* 状态机诊断：cur/evt_flag/心跳计数 */
#define TH_DIAG_PRINT_EN        0   /* 线程调度诊断：全线程 stat/prio 转储 */
#define LED_PRINT_EN            0   /* LED：翻转打印（1s 一次，带 us 时间戳） */
#define ROD_PRINT_EN            0   /* 推杆：状态/限位/霍尔故障 */
#define HALL_ROD_PRINT_EN       0   /* 推杆霍尔：限位/双高故障 */
#define HALL_MOTOR_PRINT_EN     0   /* 电机霍尔：初始化/周期观测 */
#define MONITOR_PRINT_EN        1   /* 采样监视（1s 周期打印） */
#define QUEUE_INIT_PRINT_EN     0   /* 队列初始化（示例，默认关闭） */
#define MONITOR_SYS_PRINT_EN    1   /* 系统1s打印 */
#define ROD_POS_PRINT_EN        1   /* 推杆位置/校准 1s 周期打印（转动模式：单位=度） */
#define RTT_PRINTF_EN           0   /* RT-Thread rt_kprintf 重定向到 RTT（rt_hw_console_output） */
#define TASK_STACK_PRINT_EN     0   /* 任务栈统一管理：各线程栈大小/总栈/堆余量 */
#define PWM_PRINT_EN            0   /* PWM 调速：状态切换/启动/停止（ramp 过程不打） */
#define MOTOR_GPIO_PRINT_EN     0   /* 电机 GPIO：init/成对写方向（无 ramp 过程） */
#define FLASH_PRINT_EN          0   /* Flash 驱动：擦/写/解锁流程（量大，默认关） */
#define PARAM_PRINT_EN          1   /* 参数管理：加载/保存/默认值 */
#define PARAM_WARNNING_PRINT_EN 1   /* 参数异常告警：param_set 越界拒收/上电加载越界（仅出错时打印，正常零输出） */
#define RODP_PRINT_EN           1   /* 快块B推杆行程：加载/保存/错误（[RODP]） */

/* ---- components_test 组件功能验证（批次①：内核对象） ---- */
#define KT_THREAD_PRINT_EN      1   /* 组件测试：线程（[KT][thread]） */
#define KT_SEM_PRINT_EN         1   /* 组件测试：信号量（[KT][sem]） */
#define KT_MUTEX_PRINT_EN       1   /* 组件测试：互斥量（[KT][mutex]） */
#define KT_EVENT_PRINT_EN       1   /* 组件测试：事件（[KT][event]） */
#define KT_MAIL_PRINT_EN        1   /* 组件测试：邮箱（[KT][mail]） */
#define KT_MQ_PRINT_EN          1   /* 组件测试：消息队列（[KT][mq]） */
#define KT_MP_PRINT_EN          1   /* 组件测试：内存池（[KT][mp]） */
#define KT_HEAP_PRINT_EN        1   /* 组件测试：堆（[KT][heap]） */
#define KT_TIMER_PRINT_EN       1   /* 组件测试：硬定时器（[KT][timer]） */
#define KT_IDLE_PRINT_EN        1   /* 组件测试：空闲钩子（[KT][idle]） */

/* ---- msh 输入源选择（功能开关，非打印；msh_rtt.c 使用，改后需重编译） ---- */
#define MSH_RTT_EN              1   /* 1=msh 输入绑 RTT 下行通道0（RTT Viewer Terminal 可敲命令）；0=维持 uart4 串口 */

/* ===================== 各模块打印宏封装 ===================== */
#if SYS_STATE_PRINT_EN
#define SYS_STATE_PRINT(fmt, ...)   MAIN_D("[SYS_STATE] " fmt, ##__VA_ARGS__)
#else
#define SYS_STATE_PRINT(fmt, ...)   ((void)0)
#endif

#if DEV_REG_PRINT_EN
#define DEV_REG_PRINT(fmt, ...)     MAIN_D("[DEV_REG] " fmt, ##__VA_ARGS__)
#else
#define DEV_REG_PRINT(fmt, ...)     ((void)0)
#endif

#if POWER_PRINT_EN
#define POWER_PRINT(fmt, ...)       MAIN_D("[POWER] " fmt, ##__VA_ARGS__)
#else
#define POWER_PRINT(fmt, ...)       ((void)0)
#endif

#if POLARITY_PRINT_EN
#define POLARITY_PRINT(fmt, ...)    MAIN_D("[POLARITY] " fmt, ##__VA_ARGS__)
#else
#define POLARITY_PRINT(fmt, ...)    ((void)0)
#endif

#if ARB_PRINT_EN
#define ARB_PRINT(fmt, ...)         MAIN_D("[ARB] " fmt, ##__VA_ARGS__)
#else
#define ARB_PRINT(fmt, ...)         ((void)0)
#endif

#if SM_DIAG_PRINT_EN
#define SM_DIAG_PRINT(fmt, ...)     MAIN_D("[SM_DIAG] " fmt, ##__VA_ARGS__)
#else
#define SM_DIAG_PRINT(fmt, ...)     ((void)0)
#endif

#if TH_DIAG_PRINT_EN
#define TH_DIAG_PRINT(fmt, ...)     MAIN_D("[TH_DIAG] " fmt, ##__VA_ARGS__)
#else
#define TH_DIAG_PRINT(fmt, ...)     ((void)0)
#endif

#if LED_PRINT_EN
#define LED_PRINT(fmt, ...)         MAIN_D("[LED] " fmt, ##__VA_ARGS__)
#else
#define LED_PRINT(fmt, ...)         ((void)0)
#endif

#if ROD_PRINT_EN
#define ROD_PRINT(fmt, ...)         MAIN_D("[ROD] " fmt, ##__VA_ARGS__)
#else
#define ROD_PRINT(fmt, ...)         ((void)0)
#endif

#if HALL_ROD_PRINT_EN
#define HALL_ROD_PRINT(fmt, ...)    MAIN_D("[HALL_R] " fmt, ##__VA_ARGS__)
#else
#define HALL_ROD_PRINT(fmt, ...)    ((void)0)
#endif

#if HALL_MOTOR_PRINT_EN
#define HALL_MOTOR_PRINT(fmt, ...)  MAIN_D("[HALL_M] " fmt, ##__VA_ARGS__)
#else
#define HALL_MOTOR_PRINT(fmt, ...)  ((void)0)
#endif

#if QUEUE_INIT_PRINT_EN
#define QUEUE_INIT_PRINT(fmt, ...)  MAIN_D("[QUEUE_INIT] " fmt, ##__VA_ARGS__)
#else
#define QUEUE_INIT_PRINT(fmt, ...)  ((void)0)
#endif

#if PWM_PRINT_EN
#define PWM_PRINT(fmt, ...)         MAIN_D("[PWM] " fmt, ##__VA_ARGS__)
#else
#define PWM_PRINT(fmt, ...)         ((void)0)
#endif

#if MOTOR_GPIO_PRINT_EN
#define MOTOR_GPIO_PRINT(fmt, ...)  MAIN_D("[MOTG] " fmt, ##__VA_ARGS__)
#else
#define MOTOR_GPIO_PRINT(fmt, ...)  ((void)0)
#endif

#if FLASH_PRINT_EN
#define FLASH_PRINT(fmt, ...)       MAIN_D("[FLASH] " fmt, ##__VA_ARGS__)
#else
#define FLASH_PRINT(fmt, ...)       ((void)0)
#endif

#if PARAM_PRINT_EN
#define PARAM_PRINT(fmt, ...)       MAIN_D("[PARAM] " fmt, ##__VA_ARGS__)
#else
#define PARAM_PRINT(fmt, ...)       ((void)0)
#endif

#if PARAM_WARNNING_PRINT_EN
/* [PARAM_WARNNING]：参数异常专用告警（仅检查发现错误时调用，正常路径零输出）；
   自带上下分隔线（LOG_CH 末尾自动补 \r\n），便于在日志流中一眼定位 */
#define PARAM_WARNNING(fmt, ...) \
    MAIN_D("========================================\r\n" \
           "[PARAM_WARNNING] " fmt "\r\n" \
           "========================================", ##__VA_ARGS__)
#else
#define PARAM_WARNNING(fmt, ...)    ((void)0)
#endif

#if RODP_PRINT_EN
#define RODP_PRINT(fmt, ...)        MAIN_D("[RODP] " fmt, ##__VA_ARGS__)
#else
#define RODP_PRINT(fmt, ...)        ((void)0)
#endif

/* ---- components_test 组件测试打印（明细走普通 RTT，可开关） ---- */
#if KT_THREAD_PRINT_EN
#define KT_THREAD_PRINT(fmt, ...)   MAIN_D("[KT][thread] " fmt, ##__VA_ARGS__)
#else
#define KT_THREAD_PRINT(fmt, ...)   ((void)0)
#endif

#if KT_SEM_PRINT_EN
#define KT_SEM_PRINT(fmt, ...)      MAIN_D("[KT][sem] " fmt, ##__VA_ARGS__)
#else
#define KT_SEM_PRINT(fmt, ...)      ((void)0)
#endif

#if KT_MUTEX_PRINT_EN
#define KT_MUTEX_PRINT(fmt, ...)    MAIN_D("[KT][mutex] " fmt, ##__VA_ARGS__)
#else
#define KT_MUTEX_PRINT(fmt, ...)    ((void)0)
#endif

#if KT_EVENT_PRINT_EN
#define KT_EVENT_PRINT(fmt, ...)    MAIN_D("[KT][event] " fmt, ##__VA_ARGS__)
#else
#define KT_EVENT_PRINT(fmt, ...)    ((void)0)
#endif

#if KT_MAIL_PRINT_EN
#define KT_MAIL_PRINT(fmt, ...)     MAIN_D("[KT][mail] " fmt, ##__VA_ARGS__)
#else
#define KT_MAIL_PRINT(fmt, ...)     ((void)0)
#endif

#if KT_MQ_PRINT_EN
#define KT_MQ_PRINT(fmt, ...)       MAIN_D("[KT][mq] " fmt, ##__VA_ARGS__)
#else
#define KT_MQ_PRINT(fmt, ...)       ((void)0)
#endif

#if KT_MP_PRINT_EN
#define KT_MP_PRINT(fmt, ...)       MAIN_D("[KT][mp] " fmt, ##__VA_ARGS__)
#else
#define KT_MP_PRINT(fmt, ...)       ((void)0)
#endif

#if KT_HEAP_PRINT_EN
#define KT_HEAP_PRINT(fmt, ...)     MAIN_D("[KT][heap] " fmt, ##__VA_ARGS__)
#else
#define KT_HEAP_PRINT(fmt, ...)     ((void)0)
#endif

#if KT_TIMER_PRINT_EN
#define KT_TIMER_PRINT(fmt, ...)    MAIN_D("[KT][timer] " fmt, ##__VA_ARGS__)
#else
#define KT_TIMER_PRINT(fmt, ...)    ((void)0)
#endif

#if KT_IDLE_PRINT_EN
#define KT_IDLE_PRINT(fmt, ...)     MAIN_D("[KT][idle] " fmt, ##__VA_ARGS__)
#else
#define KT_IDLE_PRINT(fmt, ...)     ((void)0)
#endif

#if RTT_PRINTF_EN
#define RTT_PRINTF(fmt, ...)    MAIN_D("[RT_PRINTF] " fmt, ##__VA_ARGS__)
#else
#define RTT_PRINTF(fmt, ...)    ((void)0)
#endif

#if MONITOR_PRINT_EN
#define MONITOR_PRINT(fmt, ...)     MAIN_D("[MON] " fmt, ##__VA_ARGS__)
#else
#define MONITOR_PRINT(fmt, ...)     ((void)0)
#endif

#if MONITOR_SYS_PRINT_EN
#define MONITOR_SYS_PRINT(fmt, ...)     MAIN_D("[SYS_MON] " fmt, ##__VA_ARGS__)
#else
#define MONITOR_SYS_PRINT(fmt, ...)     ((void)0)
#endif

#if ROD_POS_PRINT_EN
#define ROD_POS_PRINT(fmt, ...)     MAIN_D("[ROD_POS] " fmt, ##__VA_ARGS__)
#else
#define ROD_POS_PRINT(fmt, ...)     ((void)0)
#endif

#if TASK_STACK_PRINT_EN
#define TASK_STACK_PRINT(fmt, ...)     MAIN_D("[TASK_STACK] " fmt, ##__VA_ARGS__)
#else
#define TASK_STACK_PRINT(fmt, ...)     ((void)0)
#endif

/* 打印当前开关状态（调试用） */
void RttManager_DumpSwitches(void);

#endif /* __RTT_MANAGER_H__ */






