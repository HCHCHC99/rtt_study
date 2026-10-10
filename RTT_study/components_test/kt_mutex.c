/*
 * kt_mutex.c — 互斥量验证: 递归/优先级继承/恢复/静态动态
 * 命令: kt_mutex
 */
#include <rtthread.h>
#define KT_MODULE_PRINT(fmt, ...) KT_MUTEX_PRINT(fmt, ##__VA_ARGS__)
#include "kt_common.h"

static struct kt_result g_r;

static rt_mutex_t m_mux = RT_NULL;
static struct rt_semaphore m_go;    /* main -> child: 放行 */
static struct rt_semaphore m_done;  /* child -> main: 完成 */
static volatile rt_err_t    m_child_err = -1;
static volatile rt_uint32_t m_prio_before, m_prio_during, m_prio_after;
static rt_uint8_t m_saved_prio;

static void m_worker(void *p)
{
    rt_sem_take(&m_go, KT_TIMEOUT_2S);              /* 等 main 放行 */
    m_child_err = rt_mutex_take((rt_mutex_t)p, KT_TIMEOUT_2S);
    if (m_child_err == RT_EOK)
        rt_mutex_release((rt_mutex_t)p);            /* 立即归还, 触发优先级恢复 */
    rt_sem_release(&m_done);
}

void kt_mutex_test(void)
{
    rt_thread_t tid;
    rt_err_t err;
    rt_uint8_t prio_tmp;
    struct rt_mutex m_sta;

    kt_result_init(&g_r, "mutex");

    /* 0. 把 main 降到 KT_PRIO_LOW, 让"child(18) 抢 main(19) 的锁"可观测继承 */
    m_saved_prio = rt_thread_self()->current_priority;
    prio_tmp = KT_PRIO_LOW;
    err = rt_thread_control(rt_thread_self(),
                            RT_THREAD_CTRL_CHANGE_PRIORITY, &prio_tmp);
    KT_CHECK(&g_r, err == RT_EOK, "control change own priority");
    KT_CHECK(&g_r, rt_thread_self()->current_priority == KT_PRIO_LOW,
             "priority now 19");

    m_mux = rt_mutex_create("kt_m1", RT_IPC_FLAG_PRIO);
    KT_CHECK(&g_r, m_mux != RT_NULL, "rt_mutex_create != NULL");
    if (m_mux != RT_NULL)
    {
        err = rt_mutex_take(m_mux, KT_TIMEOUT_2S);
        KT_CHECK(&g_r, err == RT_EOK, "main take");
        KT_CHECK(&g_r, m_mux->owner == rt_thread_self(), "owner == self");
        KT_CHECK(&g_r, m_mux->hold == 1, "hold == 1");

        err = rt_mutex_take(m_mux, KT_TIMEOUT_2S);
        KT_CHECK(&g_r, err == RT_EOK, "recursive take");
        KT_CHECK(&g_r, m_mux->hold == 2, "hold == 2");

        err  = rt_mutex_release(m_mux);
        err |= rt_mutex_release(m_mux);
        KT_CHECK(&g_r, err == RT_EOK, "release twice");
        KT_CHECK(&g_r, m_mux->owner == RT_NULL, "owner back to NULL");

        /* 优先级继承: child(18) 阻塞在 main(19) 持有的锁上 */
        err  = rt_sem_init(&m_go,   "kt_mg", 0, RT_IPC_FLAG_FIFO);
        err |= rt_sem_init(&m_done, "kt_md", 0, RT_IPC_FLAG_FIFO);
        KT_CHECK(&g_r, err == RT_EOK, "init sync sems");

        tid = rt_thread_create("kt_mc", m_worker, m_mux,
                               KT_STACK_SMALL, KT_PRIO_MID, 10);
        KT_CHECK(&g_r, tid != RT_NULL, "create child thread");
        if (tid != RT_NULL)
        {
            m_child_err = -1;
            err = rt_mutex_take(m_mux, KT_TIMEOUT_2S);
            KT_CHECK(&g_r, err == RT_EOK, "main take again");
            m_prio_before = rt_thread_self()->current_priority;

            rt_thread_startup(tid);
            rt_thread_mdelay(20);           /* child 阻塞在 m_go */
            rt_sem_release(&m_go);          /* 放行, child 转为阻塞在 m_mux */
            rt_thread_mdelay(50);           /* 继承生效 */
            m_prio_during = rt_thread_self()->current_priority;
            KT_CHECK(&g_r, m_prio_during == KT_PRIO_MID,
                     "priority inherited to child prio(26)");

            rt_mutex_release(m_mux);        /* 归还, child(26) 抢到 */
            err = rt_sem_take(&m_done, KT_TIMEOUT_2S);
            KT_CHECK(&g_r, err == RT_EOK, "child done in 2s");
            KT_CHECK(&g_r, m_child_err == RT_EOK, "child take ok");

            rt_thread_mdelay(10);           /* 等优先级恢复 */
            m_prio_after = rt_thread_self()->current_priority;
            KT_CHECK(&g_r, m_prio_after == m_prio_before,
                     "priority restored");
        }
        rt_thread_mdelay(10);

        rt_sem_detach(&m_go);
        rt_sem_detach(&m_done);

        err = rt_mutex_delete(m_mux);
        KT_CHECK(&g_r, err == RT_EOK, "rt_mutex_delete");
    }

    /* 静态接口 */
    err = rt_mutex_init(&m_sta, "kt_m2", RT_IPC_FLAG_PRIO);
    KT_CHECK(&g_r, err == RT_EOK, "rt_mutex_init");
    err = rt_mutex_trytake(&m_sta);
    KT_CHECK(&g_r, err == RT_EOK, "trytake first");
    err = rt_mutex_trytake(&m_sta);
    KT_CHECK(&g_r, err == RT_EOK, "trytake again(recursive)");
    err  = rt_mutex_release(&m_sta);
    err |= rt_mutex_release(&m_sta);
    KT_CHECK(&g_r, err == RT_EOK, "static release twice");
    err = rt_mutex_detach(&m_sta);
    KT_CHECK(&g_r, err == RT_EOK, "rt_mutex_detach");

    /* 恢复 main 原优先级 */
    prio_tmp = m_saved_prio;
    rt_thread_control(rt_thread_self(),
                      RT_THREAD_CTRL_CHANGE_PRIORITY, &prio_tmp);
    kt_result_report(&g_r);
}
MSH_CMD_EXPORT_ALIAS(kt_mutex_test, kt_mutex, RT-Thread mutex test);
