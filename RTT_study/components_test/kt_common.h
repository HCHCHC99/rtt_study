/*
 * kt_common.h — components_test 公共定义
 *
 * 约定（所有 kt_*.c 必须遵守）：
 *  - 一组件一文件 kt_<组件>.c，测试入口 void kt_<组件>_test(void)（非 static），
 *    并用 MSH_CMD_EXPORT_ALIAS 导出同名 msh 命令；
 *  - 所有阻塞调用必须带超时（<= KT_TIMEOUT_2S），防止挂死 tshell；
 *  - 每个测试自清理（delete/detach），可重复执行；
 *  - 定时器/空闲钩子回调内禁止任何打印，只允许置计数或放信号量；
 *  - 校验一律用 KT_CHECK，结束一律 kt_result_report 打印 PASS/FAIL 并登记结果表；
 *  - 打印统一走 rtt_manager.h 的组件开关宏（开关区 KT_XXX_PRINT_EN）：
 *    各 kt_*.c 必须在 include 本文件前把 KT_MODULE_PRINT 指到本组件的宏，如
 *      #define KT_MODULE_PRINT(fmt, ...)  KT_SEM_PRINT(fmt, ##__VA_ARGS__)
 */
#ifndef __KT_COMMON_H__
#define __KT_COMMON_H__

#include <rtthread.h>
#include "rtt_manager.h"

/* 测试文件打印出口；未按约定重定义时退化为无前缀 MAIN_D */
#ifndef KT_MODULE_PRINT
#define KT_MODULE_PRINT(fmt, ...)   MAIN_D(fmt, ##__VA_ARGS__)
#endif

/* ---------- 统一常量 ---------- */
#define KT_PRIO_HIGH        17      /* 全部 ≤19：比 tshell(20) 更紧急，防 20+ 线程异常时饿死测试线程 */
#define KT_PRIO_MID         18      /* 仍 >10(main)：测试子线程只在 main 阻塞时运行，保持原时序假设 */
#define KT_PRIO_LOW         19
#define KT_STACK_SMALL      1024    /* 字节 */
#define KT_TIMEOUT_2S       (RT_TICK_PER_SECOND * 2)

/* ---------- 结果统计 ---------- */
struct kt_result
{
    const char *name;           /* 组件名，如 "sem" */
    rt_uint32_t total;
    rt_uint32_t fail;
};

/* ---------- 结果登记表声明（实现见 kt_result.c；必须位于 struct 定义之后，
   否则会变成函数原型作用域内的不完整类型，引发 conflicting types） ---------- */
void kt_result_publish(const struct kt_result *r);      /* 同名覆盖登记 */
const struct kt_result *kt_result_get(int index);       /* 越界返回 RT_NULL */
int kt_result_count(void);
void kt_summary_print(void);    /* 总表：一组件一行 PASS/FAIL + 总结论（MAIN_D_SYNC 防丢） */

static inline void kt_result_init(struct kt_result *r, const char *name)
{
    r->name  = name;
    r->total = 0;
    r->fail  = 0;
}

/* 结束汇总：打印本组件 PASS/FAIL 行，并登记到 kt_result.c 结果表
   （main 启动自跑 / kt_all 结束后由 kt_summary_print 统一打印总表） */
static inline void kt_result_report(const struct kt_result *r)
{
    KT_MODULE_PRINT("============ %s : %u/%u ============",
                    (r->fail == 0) ? "PASS" : "FAIL",
                    (unsigned)(r->total - r->fail), (unsigned)r->total);
    kt_result_publish(r);
}

/* 校验一步：cond 真记 PASS，假记 FAIL 并打印文件:行号。不中断流程。 */
#define KT_CHECK(res, cond, desc)                                   \
    do                                                              \
    {                                                               \
        (res)->total++;                                             \
        if (cond)                                                   \
            KT_MODULE_PRINT("PASS %s", desc);                       \
        else                                                        \
        {                                                           \
            (res)->fail++;                                          \
            KT_MODULE_PRINT("FAIL %s @%s:%d",                       \
                            desc, __FILE__, __LINE__);              \
        }                                                           \
    } while (0)

/* ---------- 各组件测试入口（kt_menu.c / main.c 汇总调用） ---------- */
void kt_thread_test(void);
void kt_sem_test(void);
void kt_mutex_test(void);
void kt_event_test(void);
void kt_mail_test(void);
void kt_mq_test(void);
void kt_mp_test(void);
void kt_heap_test(void);
void kt_timer_test(void);
void kt_idle_test(void);

/* ---------- 调度诊断（kt_diag.c）：线程饿死/TCB 被踩排查 ---------- */
void kt_diag_dump(const char *tag);

#endif /* __KT_COMMON_H__ */
