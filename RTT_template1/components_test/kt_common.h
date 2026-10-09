/*
 * kt_common.h — components_test 公共定义
 *
 * 约定（所有 kt_*.c 必须遵守）：
 *  - 一组件一文件 kt_<组件>.c，测试入口 void kt_<组件>_test(void)（非 static），
 *    并用 MSH_CMD_EXPORT_ALIAS 导出同名 msh 命令；
 *  - 所有阻塞调用必须带超时（<= KT_TIMEOUT_2S），防止挂死 tshell；
 *  - 每个测试自清理（delete/detach），可重复执行；
 *  - 定时器/空闲钩子回调内禁止 rt_kprintf，只允许置计数或放信号量；
 *  - 校验一律用 KT_CHECK，结束一律 kt_result_report 打印 PASS/FAIL 汇总。
 */
#ifndef __KT_COMMON_H__
#define __KT_COMMON_H__

#include <rtthread.h>

/* ---------- 统一常量 ---------- */
#define KT_PRIO_HIGH        24      /* 数值比 20(tshell) 大 = 更低 urgency，不抢 msh */
#define KT_PRIO_MID         26
#define KT_PRIO_LOW         28
#define KT_STACK_SMALL      1024    /* 字节 */
#define KT_TIMEOUT_2S       (RT_TICK_PER_SECOND * 2)

/* ---------- 结果统计 ---------- */
struct kt_result
{
    const char *name;           /* 组件名，如 "sem" */
    rt_uint32_t total;
    rt_uint32_t fail;
};

static inline void kt_result_init(struct kt_result *r, const char *name)
{
    r->name  = name;
    r->total = 0;
    r->fail  = 0;
}

static inline void kt_result_report(const struct kt_result *r)
{
    rt_kprintf("[KT][%s] ============ %s : %u/%u ============\n",
               r->name,
               (r->fail == 0) ? "PASS" : "FAIL",
               (unsigned)(r->total - r->fail), (unsigned)r->total);
}

/* 校验一步：cond 真记 PASS，假记 FAIL 并打印文件:行号。不中断流程。 */
#define KT_CHECK(res, cond, desc)                                   \
    do                                                              \
    {                                                               \
        (res)->total++;                                             \
        if (cond)                                                   \
            rt_kprintf("[KT][%s]   PASS %s\n", (res)->name, desc);  \
        else                                                        \
        {                                                           \
            (res)->fail++;                                          \
            rt_kprintf("[KT][%s]   FAIL %s @%s:%d\n",               \
                       (res)->name, desc, __FILE__, __LINE__);      \
        }                                                           \
    } while (0)

/* ---------- 各组件测试入口（kt_menu.c 汇总调用） ---------- */
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

#endif /* __KT_COMMON_H__ */
