/**
 * @file    task_set.h
 * @brief   任务配置统一管理：所有线程栈大小 + 优先级的唯一权威来源（Single Source of Truth）
 * @note    栈大小与优先级数值只在本文件改；各 task 头文件与 rtconfig.h 均用宏引用（宏引用宏）。
 *          打印走 TASK_STACK_PRINT（MAIN_D，英文整型，开关在 rtt_manager.h）。
 *          线程创建统一走 Task_Set_Create（task_set.c）：带栈/优先级合法性检查，
 *          失败会打印并返回 NULL，避免坏线程引发低优先级线程饿死。
 */
#ifndef __TASK_SET_H__
#define __TASK_SET_H__

#include <stdint.h>

/* ===================== 线程栈大小统一管理（唯一权威来源） ===================== */
#define TASK_STACK_MAIN     (4096U)   /* main 主线程：初始化 + 1s 采样打印 + MSH */
#define TASK_STACK_SYS_SM   (2048U)  /* sys_sm 系统状态机事件线程（InitAll 含仲裁复位+RTT 打印，256 太紧） */
#define TASK_STACK_DEV      (512U)   /* dev 设备管理线程 */
#define TASK_STACK_ROD      (2048U)  /* rod 推杆位置/状态（含 float 状态机 + Arb_GetData 约360B 局部拷贝 + RTT 打印；256B 曾栈溢出写坏 TCB，见 md_record/INIT卡死-线程饿死与栈溢出.md） */
#define TASK_STACK_DI       (2048U)  /* di DI 采集（Polarity_Scan 链 + RTT printf 峰值，256B 曾栈溢出） */
#define TASK_STACK_ARB      (2048U)  /* act 电机仲裁事件线程（rt_mq 阻塞 + 决策） */
#define TASK_STACK_PWM      (1024U)  /* pwm PWM 调速线程（缓启动状态机 10ms tick + 状态切换打印；旧板用，当前未注册不启用） */
#define TASK_STACK_ARB_SELFTEST (1024U)  /* arbtst 仲裁台架自测线程 */
#define TASK_STACK_LED      (256U)   /* led LED 1s 翻转 */
#define TASK_STACK_FINSH    (1024U)   /* finsh MSH shell */
#define TASK_STACK_IDLE     (256U)    /* idle 空闲线程 */
#define TASK_STACK_WORKQ    (256U)   /* workq 系统工作队列 */
#define TASK_STACK_TIMER    (512U)    /* timer 软定时器线程（RT-Thread 默认 512B） */

/* ===================== 线程优先级统一管理（唯一权威来源，数字越小优先级越高） ===================== */
/* main 由 rtconfig.h 的 RT_MAIN_THREAD_PRIORITY 设定（当前 10），此处仅登记；
   tshell/workq/timer/idle 等 kernel 线程优先级在 rtconfig.h，不在此处 */
#define TASK_PRIO_ACT           (15U)   /* act 电机仲裁事件线程 */
#define TASK_PRIO_PWM           (16U)   /* pwm PWM 调速线程（紧随 act，10ms 缓启动 tick；旧板用，当前未注册不启用） */
#define TASK_PRIO_LED           (19U)   /* led LED 1s 翻转 */
#define TASK_PRIO_SYS_SM        (19U)   /* sys_sm 系统状态机事件线程 */
#define TASK_PRIO_DEV           (18U)   /* dev 设备管理（monitor B 模式） */
#define TASK_PRIO_ROD           (18U)   /* rod 推杆位置/状态 */
#define TASK_PRIO_DI            (18U)   /* di DI 采集（2ms 极性扫描） */
#define TASK_PRIO_ARB_SELFTEST  (15U)   /* arbtst 仲裁台架自测 */
#define TASK_PRIO_CANARY        (2U)   /* canary 饿死金丝雀（仅高于 idle） */

/* ===================== 线程创建统一入口 + 饿死防护 ===================== */
#define TASK_STACK_MIN                  (256U)    /* 允许创建的最小栈（字节） */
#define TASK_SET_STARVATION_GUARD_EN    1         /* 1=启动 canary 金丝雀 + 主循环饿死告警 */
#define TASK_SET_CANARY_PERIOD_MS       (500U)    /* 金丝雀心跳周期 */
#define TASK_SET_STARVATION_TIMEOUT_MS  (3000U)   /* 金丝雀超时未跑 → 告警 */
#define TASK_STACK_CANARY               (256U)    /* canary 栈大小 */
#define TASK_SET_BEAT_TIMEOUT_MS        (5000U)   /* 任务心跳超时 → 判定饿死 */

/* 任务栈登记项 */
typedef struct {
    const char *name;      /* 线程名（与 rt_thread_create 的 name 一致） */
    uint32_t    stack;     /* 栈大小（字节） */
    uint32_t    min_free;  /* 运行期哨兵扫描到的最低剩余字节 */
    uint8_t     tracked;   /* 是否已在运行线程列表中匹配到该线程 */
} TaskStackItem_t;

/* 打印所有任务栈大小 + 总栈 + 堆余量（MAIN_D，英文整型） */
void Task_Stack_Dump(void);

/* 统一线程创建：栈/优先级合法性检查 + 失败打印，成功即启动。
   返回线程句柄（NULL=失败）。entry 签名与 rt_thread_create 一致。
   beat_ms：周期任务填循环周期（循环内须调 Task_Set_Beat），事件驱动填 0（不参与饿死检测）。
   注意：本头文件被 rtconfig.h 包含，不能包含 rtthread.h，故用 void* 返回。 */
void *Task_Set_Create(const char *name, void (*entry)(void *), void *param,
                      uint32_t stack, uint8_t prio, uint32_t beat_ms);
/* 周期任务循环内调用一次：刷新当前线程心跳（供饿死检测点名） */
void Task_Set_Beat(void);
/* 启动饿死防护金丝雀（main 初始化时调用一次） */
void Task_Set_Start(void);
/* 主循环周期调用：金丝雀超时未运行 → 打印饿死告警 */
void Task_Set_StarvationCheck(void);

/* 哨兵水位告警阈值：线程栈已用 >= 该百分比时打印（%），0=关闭 */
#define TASK_STACK_WARN_PCT   (90U)

/*
 * 周期监控所有线程栈：扫描 '#' 哨兵水位，超过 TASK_STACK_WARN_PCT 才打印
 * （打印 name / sp / stack_addr / stack_size / used%%），避免刷屏。
 * 建议在 1s 周期任务里调用；内部关中断遍历，可在线程上下文调用。
 */
void Task_Stack_Monitor(void);

#endif /* __TASK_SET_H__ */
